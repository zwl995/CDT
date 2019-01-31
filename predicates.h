/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                                 *
 * Copyright (c) 2016, William C. Lenthe                                           *
 * All rights reserved.                                                            *
 *                                                                                 *
 * Redistribution and use in source and binary forms, with or without              *
 * modification, are permitted provided that the following conditions are met:     *
 *                                                                                 *
 * 1. Redistributions of source code must retain the above copyright notice, this  *
 *    list of conditions and the following disclaimer.                             *
 *                                                                                 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,    *
 *    this list of conditions and the following disclaimer in the documentation    *
 *    and/or other materials provided with the distribution.                       *
 *                                                                                 *
 * 3. Neither the name of the copyright holder nor the names of its                *
 *    contributors may be used to endorse or promote products derived from         *
 *    this software without specific prior written permission.                     *
 *                                                                                 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"     *
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE       *
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE  *
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE    *
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL      *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR      *
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER      *
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   *
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE   *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.            *
 *                                                                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#pragma once

#include <array>
#include <cmath>//abs, fma, exp2, signbit
#include <limits>
#include <utility>//pair
#include <numeric>//accumulate
#include <algorithm>//transform, copy_n, merge
#include <functional>//negate
#include <type_traits>//is_same, enable_if

template<typename T> class ExpansionBase;

template<typename T, size_t N>
class Expansion : private ExpansionBase<T>, public std::array<T, N> {
	private:
	public:
		size_t m_size;
		template <typename S> friend class ExpansionBase;//access for base class
		template <typename S, size_t M> friend class Expansion;//access for expansions of different size

		Expansion() : m_size(0) {}
		template <size_t M> Expansion& operator=(const Expansion<T, M>& e) {
			static_assert(M <= N, "cannot assign a larger expansion to a smaller expansion");
			std::copy_n(e.cbegin(), e.size(), std::array<T, N>::begin());
			m_size = e.size();
			return *this;
		}

		//vector like convenience functions
		size_t size() const {return m_size;}
		bool empty() const {return 0 == m_size;}
		void push_back(const T v) {std::array<T, N>::operator[](m_size++) = v;}

	public:
		//estimates of expansion value
		T mostSignificant() const {return empty() ? T(0) : std::array<T, N>::operator[](m_size - 1);}
		T estimate() const {return std::accumulate(std::array<T, N>::cbegin(), std::array<T, N>::cbegin() + size(), T(0));}

		template <size_t M> Expansion<T, N+M> operator+(const Expansion<T, M>& f) const {
			Expansion<T, N+M> h;
			h.m_size = ExpansionBase<T>::ExpansionSum(this->data(), this->size(), f.data(), f.size(), h.data());
			return h;
		}

		void negate() {std::transform(std::array<T, N>::cbegin(), std::array<T, N>::cbegin() + size(), std::array<T, N>::begin(), std::negate<T>());}
		Expansion operator-() const {Expansion e = *this; e.negate(); return e;}
		template <size_t M> Expansion<T, N+M> operator-(const Expansion<T, M>& f) const {return operator+(-f);}

		Expansion<T, 2*N> operator*(const T b) const {
			Expansion<T, 2*N> h;
			h.m_size = ExpansionBase<T>::ScaleExpansion(this->data(), this->size(), b, h.data());
			return h;
		}
};

//std::fma is faster than dekker's product when the processor instruction is available
#ifdef FP_FAST_FMAF
	static const bool fp_fast_fmaf = true;
#else
	static const bool fp_fast_fmaf = false;
#endif

#ifdef FP_FAST_FMA
	static const bool fp_fast_fma = true;
#else
	static const bool fp_fast_fma = false;
#endif

#ifdef FP_FAST_FMAL
	static const bool fp_fast_fmal = true;
#else
	static const bool fp_fast_fmal = false;
#endif

template <typename T> struct use_fma {static const bool value = (std::is_same<T, float>::value       && fp_fast_fmaf) ||
                                                                (std::is_same<T, double>::value      && fp_fast_fma)  ||
                                                                (std::is_same<T, long double>::value && fp_fast_fmal);};

template<typename T>
class ExpansionBase {
	private:
		static const T Splitter;
		static_assert(std::numeric_limits<T>::is_iec559, "Requires IEC 559 / IEEE 754 floating point type");
		static_assert(2 == std::numeric_limits<T>::radix, "Requires base 2 floating point type");

		//combine result + roundoff error into expansion
		static inline Expansion<T, 2> MakeExpansion(const T value, const T tail) {
			Expansion<T, 2> e;
			if(T(0) != tail) e.push_back(tail);
			if(T(0) != value) e.push_back(value);
			return e;
		}

	protected:
		//add 2 expansions
		static size_t ExpansionSum(T const * const e, const size_t n, T const * const f, const size_t m, T * const h) {
			std::merge(e, e + n, f, f + m, h, [](const T& a, const T& b) {return std::abs(a) < std::abs(b);});
			if(m == 0) return n;
			if(n == 0) return m;
			size_t hIndex = 0;
			T Q = h[0];
			T Qnew = h[1] + Q;
			T hh = FastPlusTail(h[1], Q, Qnew);
			Q = Qnew;
			if(T(0) != hh) h[hIndex++] = hh;
			for(size_t g = 2; g != n + m; ++g) {
				Qnew = Q + h[g];
				hh = PlusTail(Q, h[g], Qnew);
				Q = Qnew;
				if(T(0) != hh) h[hIndex++] = hh;
			}
			if(T(0) != Q) h[hIndex++] = Q;
			return hIndex;
		}

		//scale an expansion by a constant
		static size_t ScaleExpansion(T const * const e, const size_t n, const T b, T * const h) {
			if(n == 0 || T(0) == b) return 0;
			size_t hIndex = 0;
			T Q = e[0] * b;
			const std::pair<T, T> bSplit = Split(b);
			T hh = MultTailPreSplit(e[0], b, bSplit, Q);
			if(T(0) != hh) h[hIndex++] = hh;
			for(size_t eIndex = 1; eIndex < n; ++eIndex) {
				T Ti = e[eIndex] * b;
				T ti = MultTailPreSplit(e[eIndex], b, bSplit, Ti);
				T Qi = Q + ti;
				hh = PlusTail(Q, ti, Qi);
				if(T(0) != hh) h[hIndex++] = hh;
				Q = Ti + Qi;
				hh = FastPlusTail(Ti, Qi, Q);
				if(T(0) != hh) h[hIndex++] = hh;
			}
			if(T(0) != Q) h[hIndex++] = Q;
			return hIndex;
		}
	
	public:
		//roundoff error of x = a + b
		static inline T PlusTail(const T a, const T b, const T x) {
			const T bVirtual = x - a;
			const T aVirtual = x - bVirtual;
			const T bRoundoff = b - bVirtual;
			const T aRoundoff = a - aVirtual;
			return aRoundoff + bRoundoff;
		}

		//roundoff error of x = a + b if |a| > |b|
		static inline T FastPlusTail(const T a, const T b, const T x) {
			const T bVirtual = x - a;
			return b - bVirtual;
		}

		//roundoff error of x = a - b
		static inline T MinusTail(const T a, const T b, const T x) {
			const T bVirtual = a - x;
			const T aVirtual = x + bVirtual;
			const T bRoundoff = bVirtual - b;
			const T aRoundoff = a - aVirtual;
			return aRoundoff + bRoundoff;
		}

		//split a into 2 nonoverlapping values
		static inline std::pair<T, T> Split(const T a) {
			const T c = Splitter * a;
			const T aBig = c - a;
			const T aHi = c - aBig;
			return std::pair<T, T>(aHi, a - aHi);
		}

		//roundoff error of x = a * b via dekkers product
        static inline T DekkersProduct(const T /*a*/, const std::pair<T, T> aSplit, const T /*b*/, const std::pair<T, T> bSplit, const T p) {
			T y = p - T(aSplit.first * bSplit.first);
			y -= T(aSplit.second * bSplit.first);
			y -= T(aSplit.first * bSplit.second);
			return T(aSplit.second * bSplit.second) - y;
		}

		//roundoff error of x = a * b
		template <typename S = T> static typename std::enable_if< use_fma<S>::value, S>::type MultTail(const T a, const T b, const T p) {return std::fma(a, b, -p);}
		template <typename S = T> static typename std::enable_if<!use_fma<S>::value, S>::type MultTail(const T a, const T b, const T p) {return DekkersProduct(a, Split(a), b, Split(b), p);}

        template <typename S = T> static typename std::enable_if< use_fma<S>::value, S>::type MultTailPreSplit(const T a, const T b, const std::pair<T, T> /*bSplit*/, const T p) {return std::fma(a, b, -p);}
		template <typename S = T> static typename std::enable_if<!use_fma<S>::value, S>::type MultTailPreSplit(const T a, const T b, const std::pair<T, T> bSplit, const T p) {return DekkersProduct(a, Split(a), b, bSplit, p);}

		//expand a + b
		static inline Expansion<T, 2> Plus(const T a, const T b) {
			const T x = a + b;
			return MakeExpansion(x, PlusTail(a, b, x));
		}

		//expand a - b
		static inline Expansion<T, 2> Minus(const T a, const T b) {return Plus(a, -b);}

		//expand a * b
		static inline Expansion<T, 2> Mult(const T a, const T b) {
			const T x = a * b;
			return MakeExpansion(x, MultTail(a, b, x));
		}

		//expand the determinant of {{ax, ay}, {bx, by}} (unrolled Mult(ax, by) - Mult(ay, bx))
		static inline Expansion<T, 4> TwoTwoDiff(const T ax, const T by, const T ay, const T bx) {
			const T axby1 = ax * by;
			const T axby0 = MultTail(ax, by, axby1);
			const T bxay1 = bx * ay;
			const T bxay0 = MultTail(bx, ay, bxay1);
			const T _i0 = axby0 - bxay0;
			const T x0 = MinusTail(axby0, bxay0, _i0);
			const T _j = axby1 + _i0;
			const T _0 = PlusTail(axby1, _i0, _j);
			const T _i1 = _0 - bxay1;
			const T x1 = MinusTail(_0, bxay1, _i1);
			const T x3 = _j + _i1;
			const T x2 = PlusTail(_j, _i1, x3);
			Expansion<T, 4> e;
			if(T(0) != x0) e.push_back(x0);
			if(T(0) != x1) e.push_back(x1);
			if(T(0) != x2) e.push_back(x2);
			if(T(0) != x3) e.push_back(x3);
			return e;
		}

		//TwoTwoDiff checking for zeros to avoid extra splitting
		static inline Expansion<T, 4> TwoTwoDiffZeroCheck(const T ax, const T by, const T ay, const T bx) {
			Expansion<T, 4> e;
			if(T(0) == ax && T(0) == ay) return e;
			else if(T(0) == ax) e = Mult(ay, bx);
			else if(T(0) == ay) e = Mult(ax, by);
			else e = TwoTwoDiff(ax, by, ay, bx);
			return e;
		}

		//(a * b) * c checking for zeros
		static inline Expansion<T, 4> ThreeProd(const T a, const T b, const T c) {return (T(0) == a || T(0) == b || T(0) == c) ? Expansion<T, 4>() : Mult(a, b) * c;}
};

template <typename T> const T ExpansionBase<T>::Splitter = std::exp2(T((std::numeric_limits<T>::digits + (std::numeric_limits<T>::digits%2))/2)) + T(1);

namespace  predicates {
	namespace exact {
		template <typename T> T orient2d(const T* pa, const T* pb, const T* pc) {
			const Expansion<T, 4> aterms = ExpansionBase<T>::TwoTwoDiff(pa[0], pb[1], pa[0], pc[1]);
			const Expansion<T, 4> bterms = ExpansionBase<T>::TwoTwoDiff(pb[0], pc[1], pb[0], pa[1]);
			const Expansion<T, 4> cterms = ExpansionBase<T>::TwoTwoDiff(pc[0], pa[1], pc[0], pb[1]);
			const Expansion<T, 12> w = aterms + bterms + cterms;
			return w.mostSignificant();
		}

		template <typename T> T incircle(const T* pa, const T* pb, const T* pc, const T* pd) {
			const Expansion<T, 4> ab = ExpansionBase<T>::TwoTwoDiff(pa[0], pb[1], pb[0], pa[1]);
			const Expansion<T, 4> bc = ExpansionBase<T>::TwoTwoDiff(pb[0], pc[1], pc[0], pb[1]);
			const Expansion<T, 4> cd = ExpansionBase<T>::TwoTwoDiff(pc[0], pd[1], pd[0], pc[1]);
			const Expansion<T, 4> da = ExpansionBase<T>::TwoTwoDiff(pd[0], pa[1], pa[0], pd[1]);
			const Expansion<T, 4> ac = ExpansionBase<T>::TwoTwoDiff(pa[0], pc[1], pc[0], pa[1]);
			const Expansion<T, 4> bd = ExpansionBase<T>::TwoTwoDiff(pb[0], pd[1], pd[0], pb[1]);

			const Expansion<T, 12> abc = ab + bc - ac;
			const Expansion<T, 12> bcd = bc + cd - bd;
			const Expansion<T, 12> cda = cd + da + ac;
			const Expansion<T, 12> dab = da + ab + bd;

			const Expansion<T, 96> adet = bcd * pa[0] *  pa[0] + bcd * pa[1] *  pa[1];
			const Expansion<T, 96> bdet = cda * pb[0] * -pb[0] + cda * pb[1] * -pb[1];
			const Expansion<T, 96> cdet = dab * pc[0] *  pc[0] + dab * pc[1] *  pc[1];
			const Expansion<T, 96> ddet = abc * pd[0] * -pd[0] + abc * pd[1] * -pd[1];

			const Expansion<T, 384> deter = (adet + bdet) + (cdet + ddet);
			return deter.mostSignificant();
		}

		template <typename T> T orient3d(const T* pa, const T* pb, const T* pc, const T* pd) {
			const Expansion<T, 4> ab = ExpansionBase<T>::TwoTwoDiff(pa[0], pb[1], pb[0], pa[1]);
			const Expansion<T, 4> bc = ExpansionBase<T>::TwoTwoDiff(pb[0], pc[1], pc[0], pb[1]);
			const Expansion<T, 4> cd = ExpansionBase<T>::TwoTwoDiff(pc[0], pd[1], pd[0], pc[1]);
			const Expansion<T, 4> da = ExpansionBase<T>::TwoTwoDiff(pd[0], pa[1], pa[0], pd[1]);
			const Expansion<T, 4> ac = ExpansionBase<T>::TwoTwoDiff(pa[0], pc[1], pc[0], pa[1]);
			const Expansion<T, 4> bd = ExpansionBase<T>::TwoTwoDiff(pb[0], pd[1], pd[0], pb[1]);

			const Expansion<T, 12> abc = ab + bc - ac;
			const Expansion<T, 12> bcd = bc + cd - bd;
			const Expansion<T, 12> cda = cd + da + ac;
			const Expansion<T, 12> dab = da + ab + bd;

			const Expansion<T, 24> adet = bcd *  pa[2];
			const Expansion<T, 24> bdet = cda * -pb[2];
			const Expansion<T, 24> cdet = dab *  pc[2];
			const Expansion<T, 24> ddet = abc * -pd[2];

			const Expansion<T, 96> deter = (adet + bdet) + (cdet + ddet);
			return deter.mostSignificant();
		}

		template <typename T> T insphere(const T* pa, const T* pb, const T* pc, const T* pd, const T* pe) {
			const Expansion<T, 4> ab = ExpansionBase<T>::TwoTwoDiff(pa[0], pb[1], pb[0], pa[1]);
			const Expansion<T, 4> bc = ExpansionBase<T>::TwoTwoDiff(pb[0], pc[1], pc[0], pb[1]);
			const Expansion<T, 4> cd = ExpansionBase<T>::TwoTwoDiff(pc[0], pd[1], pd[0], pc[1]);
			const Expansion<T, 4> de = ExpansionBase<T>::TwoTwoDiff(pd[0], pe[1], pe[0], pd[1]);
			const Expansion<T, 4> ea = ExpansionBase<T>::TwoTwoDiff(pe[0], pa[1], pa[0], pe[1]);
			const Expansion<T, 4> ac = ExpansionBase<T>::TwoTwoDiff(pa[0], pc[1], pc[0], pa[1]);
			const Expansion<T, 4> bd = ExpansionBase<T>::TwoTwoDiff(pb[0], pd[1], pd[0], pb[1]);
			const Expansion<T, 4> ce = ExpansionBase<T>::TwoTwoDiff(pc[0], pe[1], pe[0], pc[1]);
			const Expansion<T, 4> da = ExpansionBase<T>::TwoTwoDiff(pd[0], pa[1], pa[0], pd[1]);
			const Expansion<T, 4> eb = ExpansionBase<T>::TwoTwoDiff(pe[0], pb[1], pb[0], pe[1]);

			const Expansion<T, 24> abc = bc * pa[2] + ac * -pb[2] + ab * pc[2];
			const Expansion<T, 24> bcd = cd * pb[2] + bd * -pc[2] + bc * pd[2];
			const Expansion<T, 24> cde = de * pc[2] + ce * -pd[2] + cd * pe[2];
			const Expansion<T, 24> dea = ea * pd[2] + da * -pe[2] + de * pa[2];
			const Expansion<T, 24> eab = ab * pe[2] + eb * -pa[2] + ea * pb[2];
			const Expansion<T, 24> abd = bd * pa[2] + da *  pb[2] + ab * pd[2];
			const Expansion<T, 24> bce = ce * pb[2] + eb *  pc[2] + bc * pe[2];
			const Expansion<T, 24> cda = da * pc[2] + ac *  pd[2] + cd * pa[2];
			const Expansion<T, 24> deb = eb * pd[2] + bd *  pe[2] + de * pb[2];
			const Expansion<T, 24> eac = ac * pe[2] + ce *  pa[2] + ea * pc[2];

			const Expansion<T, 96> bcde = (cde + bce) - (deb + bcd);
			const Expansion<T, 96> cdea = (dea + cda) - (eac + cde);
			const Expansion<T, 96> deab = (eab + deb) - (abd + dea);
			const Expansion<T, 96> eabc = (abc + eac) - (bce + eab);
			const Expansion<T, 96> abcd = (bcd + abd) - (cda + abc);

			const Expansion<T, 1152> adet = bcde * pa[0] * pa[0] + bcde * pa[1] * pa[1] + bcde * pa[2] * pa[2];
			const Expansion<T, 1152> bdet = cdea * pb[0] * pb[0] + cdea * pb[1] * pb[1] + cdea * pb[2] * pb[2];
			const Expansion<T, 1152> cdet = deab * pc[0] * pc[0] + deab * pc[1] * pc[1] + deab * pc[2] * pc[2];
			const Expansion<T, 1152> ddet = eabc * pd[0] * pd[0] + eabc * pd[1] * pd[1] + eabc * pd[2] * pd[2];
			const Expansion<T, 1152> edet = abcd * pe[0] * pe[0] + abcd * pe[1] * pe[1] + abcd * pe[2] * pe[2];

			const Expansion<T, 5760> deter = (adet + bdet) + ((cdet + ddet) + edet);
			return deter.mostSignificant();
		}
	}

	template <typename T>
	class Constants {
		public:
			static const T epsilon, resulterrbound;
			static const T ccwerrboundA, ccwerrboundB, ccwerrboundC;
			static const T o3derrboundA, o3derrboundB, o3derrboundC;
			static const T iccerrboundA, iccerrboundB, iccerrboundC;
			static const T isperrboundA, isperrboundB, isperrboundC;
	};

	template <typename T> const T Constants<T>::epsilon        = std::exp2(T(-std::numeric_limits<T>::digits));
	template <typename T> const T Constants<T>::resulterrbound = (T( 3) + T(   8) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::ccwerrboundA   = (T( 3) + T(  16) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::ccwerrboundB   = (T( 2) + T(  12) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::ccwerrboundC   = (T( 9) + T(  64) * Constants<T>::epsilon) * Constants<T>::epsilon * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::o3derrboundA   = (T( 7) + T(  56) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::o3derrboundB   = (T( 3) + T(  28) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::o3derrboundC   = (T(26) + T( 288) * Constants<T>::epsilon) * Constants<T>::epsilon * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::iccerrboundA   = (T(10) + T(  96) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::iccerrboundB   = (T( 4) + T(  48) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::iccerrboundC   = (T(44) + T( 576) * Constants<T>::epsilon) * Constants<T>::epsilon * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::isperrboundA   = (T(16) + T( 224) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::isperrboundB   = (T( 5) + T(  72) * Constants<T>::epsilon) * Constants<T>::epsilon;
	template <typename T> const T Constants<T>::isperrboundC   = (T(71) + T(1408) * Constants<T>::epsilon) * Constants<T>::epsilon * Constants<T>::epsilon;

	namespace adaptive {
		template <typename T> T orient2d(const T* pa, const T* pb, const T* pc) {
			const T acx = pa[0] - pc[0];
			const T bcx = pb[0] - pc[0];
			const T acy = pa[1] - pc[1];
			const T bcy = pb[1] - pc[1];
			const T detleft = acx * bcy;
			const T detright = acy * bcx;
			T det = detleft - detright;
			if(std::signbit(detleft) != std::signbit(detright)) return det;
			if(T(0) == detleft || T(0) == detright) return det;

			const T detsum = std::abs(detleft + detright);
			T errbound = Constants<T>::ccwerrboundA * detsum;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const Expansion<T, 4> B = ExpansionBase<T>::TwoTwoDiff(acx, bcy, acy, bcx);
			det = B.estimate();
			errbound = Constants<T>::ccwerrboundB * detsum;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const T acxtail = ExpansionBase<T>::MinusTail(pa[0], pc[0], acx);
			const T bcxtail = ExpansionBase<T>::MinusTail(pb[0], pc[0], bcx);
			const T acytail = ExpansionBase<T>::MinusTail(pa[1], pc[1], acy);
			const T bcytail = ExpansionBase<T>::MinusTail(pb[1], pc[1], bcy);
			if(T(0) == acxtail && T(0) == bcxtail && T(0) == acytail && T(0) == bcytail) return det;

			errbound = Constants<T>::ccwerrboundC * detsum + Constants<T>::resulterrbound * std::abs(det);
			det += (acx * bcytail + bcy * acxtail) - (acy * bcxtail + bcx * acytail);
			if(std::abs(det) >= std::abs(errbound)) return det;

			const Expansion<T, 16> D = ((B + ExpansionBase<T>::TwoTwoDiff(acxtail, bcy, acytail, bcx)) + ExpansionBase<T>::TwoTwoDiff(acx, bcytail, acy, bcxtail)) + ExpansionBase<T>::TwoTwoDiff(acxtail, bcytail, acytail, bcxtail);
			return D.mostSignificant();
		}

		template <typename T> T incircle(const T* pa, const T* pb, const T* pc, const T* pd) {
			const T adx = pa[0] - pd[0];
			const T bdx = pb[0] - pd[0];
			const T cdx = pc[0] - pd[0];
			const T ady = pa[1] - pd[1];
			const T bdy = pb[1] - pd[1];
			const T cdy = pc[1] - pd[1];
			const T bdxcdy = bdx * cdy;
			const T cdxbdy = cdx * bdy;
			const T cdxady = cdx * ady;
			const T adxcdy = adx * cdy;
			const T adxbdy = adx * bdy;
			const T bdxady = bdx * ady;
			const T alift = adx * adx + ady * ady;
			const T blift = bdx * bdx + bdy * bdy;
			const T clift = cdx * cdx + cdy * cdy;
			T det = alift * (bdxcdy - cdxbdy) + blift * (cdxady - adxcdy) + clift * (adxbdy - bdxady);
			const T permanent = (std::abs(bdxcdy) + std::abs(cdxbdy)) * alift
			                  + (std::abs(cdxady) + std::abs(adxcdy)) * blift
			                  + (std::abs(adxbdy) + std::abs(bdxady)) * clift;
			T errbound = Constants<T>::iccerrboundA * permanent;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const Expansion<T, 4> bc = ExpansionBase<T>::TwoTwoDiff(bdx, cdy, cdx, bdy);
			const Expansion<T, 4> ca = ExpansionBase<T>::TwoTwoDiff(cdx, ady, adx, cdy);
			const Expansion<T, 4> ab = ExpansionBase<T>::TwoTwoDiff(adx, bdy, bdx, ady);
			const Expansion<T, 32> adet = bc * adx * adx + bc * ady * ady;
			const Expansion<T, 32> bdet = ca * bdx * bdx + ca * bdy * bdy;
			const Expansion<T, 32> cdet = ab * cdx * cdx + ab * cdy * cdy;
			const Expansion<T, 96> fin1 = adet + bdet + cdet;
			det = fin1.estimate();
			errbound = Constants<T>::iccerrboundB * permanent;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const T adxtail = ExpansionBase<T>::MinusTail(pa[0], pd[0], adx);
			const T adytail = ExpansionBase<T>::MinusTail(pa[1], pd[1], ady);
			const T bdxtail = ExpansionBase<T>::MinusTail(pb[0], pd[0], bdx);
			const T bdytail = ExpansionBase<T>::MinusTail(pb[1], pd[1], bdy);
			const T cdxtail = ExpansionBase<T>::MinusTail(pc[0], pd[0], cdx);
			const T cdytail = ExpansionBase<T>::MinusTail(pc[1], pd[1], cdy);
			if(T(0) == adxtail && T(0) == bdxtail && T(0) == cdxtail && T(0) == adytail && T(0) == bdytail && T(0) == cdytail) return det;

			errbound = Constants<T>::iccerrboundC * permanent + Constants<T>::resulterrbound * std::abs(det);
			det += ((adx * adx + ady * ady) * ((bdx * cdytail + cdy * bdxtail) - (bdy * cdxtail + cdx * bdytail))
			    +   (bdx * cdy - bdy * cdx) *  (adx * adxtail + ady * adytail) * T(2))
			    +  ((bdx * bdx + bdy * bdy) * ((cdx * adytail + ady * cdxtail) - (cdy * adxtail + adx * cdytail))
			    +   (cdx * ady - cdy * adx) *  (bdx * bdxtail + bdy * bdytail) * T(2))
			    +  ((cdx * cdx + cdy * cdy) * ((adx * bdytail + bdy * adxtail) - (ady * bdxtail + bdx * adytail))
			    +   (adx * bdy - ady * bdx) *  (cdx * cdxtail + cdy * cdytail) * T(2));
			if(std::abs(det) >= std::abs(errbound)) return det;
			return exact::incircle(pa, pb, pc, pd);
		}

		template <typename T> T orient3d(const T* pa, const T* pb, const T* pc, const T* pd) {
			const T adx = pa[0] - pd[0];
			const T bdx = pb[0] - pd[0];
			const T cdx = pc[0] - pd[0];
			const T ady = pa[1] - pd[1];
			const T bdy = pb[1] - pd[1];
			const T cdy = pc[1] - pd[1];
			const T adz = pa[2] - pd[2];
			const T bdz = pb[2] - pd[2];
			const T cdz = pc[2] - pd[2];
			const T bdxcdy = bdx * cdy;
			const T cdxbdy = cdx * bdy;
			const T cdxady = cdx * ady;
			const T adxcdy = adx * cdy;
			const T adxbdy = adx * bdy;
			const T bdxady = bdx * ady;
			T det = adz * (bdxcdy - cdxbdy) + bdz * (cdxady - adxcdy) + cdz * (adxbdy - bdxady);
			const T permanent = (std::abs(bdxcdy) + std::abs(cdxbdy)) * std::abs(adz) + (std::abs(cdxady) + std::abs(adxcdy)) * std::abs(bdz) + (std::abs(adxbdy) + std::abs(bdxady)) * std::abs(cdz);
			T errbound = Constants<T>::o3derrboundA * permanent;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const Expansion<T, 4> bc = ExpansionBase<T>::TwoTwoDiff(bdx, cdy, cdx, bdy);
			const Expansion<T, 4> ca = ExpansionBase<T>::TwoTwoDiff(cdx, ady, adx, cdy);
			const Expansion<T, 4> ab = ExpansionBase<T>::TwoTwoDiff(adx, bdy, bdx, ady);
			const Expansion<T, 24> fin1 = (bc * adz + ca * bdz) + ab * cdz;
			det = fin1.estimate();
			errbound = Constants<T>::o3derrboundB * permanent;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const T adxtail = ExpansionBase<T>::MinusTail(pa[0], pd[0], adx);
			const T bdxtail = ExpansionBase<T>::MinusTail(pb[0], pd[0], bdx);
			const T cdxtail = ExpansionBase<T>::MinusTail(pc[0], pd[0], cdx);
			const T adytail = ExpansionBase<T>::MinusTail(pa[1], pd[1], ady);
			const T bdytail = ExpansionBase<T>::MinusTail(pb[1], pd[1], bdy);
			const T cdytail = ExpansionBase<T>::MinusTail(pc[1], pd[1], cdy);
			const T adztail = ExpansionBase<T>::MinusTail(pa[2], pd[2], adz);
			const T bdztail = ExpansionBase<T>::MinusTail(pb[2], pd[2], bdz);
			const T cdztail = ExpansionBase<T>::MinusTail(pc[2], pd[2], cdz);
			if(T(0) == adxtail && T(0) == adytail && T(0) == adztail &&
			   T(0) == bdxtail && T(0) == bdytail && T(0) == bdztail &&
			   T(0) == cdxtail && T(0) == cdytail && T(0) == cdztail) return det;

			errbound = Constants<T>::o3derrboundC * permanent + Constants<T>::resulterrbound * std::abs(det);
			det += (adz * ((bdx * cdytail + cdy * bdxtail) - (bdy * cdxtail + cdx * bdytail)) + adztail * (bdx * cdy - bdy * cdx))
			    +  (bdz * ((cdx * adytail + ady * cdxtail) - (cdy * adxtail + adx * cdytail)) + bdztail * (cdx * ady - cdy * adx))
			    +  (cdz * ((adx * bdytail + bdy * adxtail) - (ady * bdxtail + bdx * adytail)) + cdztail * (adx * bdy - ady * bdx));
			if(std::abs(det) >= std::abs(errbound)) return det;

			Expansion<T, 8> bct = ExpansionBase<T>::TwoTwoDiffZeroCheck(bdxtail, cdy, bdytail, cdx) + ExpansionBase<T>::TwoTwoDiffZeroCheck(cdytail, bdx, cdxtail, bdy);
			Expansion<T, 8> cat = ExpansionBase<T>::TwoTwoDiffZeroCheck(cdxtail, ady, cdytail, adx) + ExpansionBase<T>::TwoTwoDiffZeroCheck(adytail, cdx, adxtail, cdy);
			Expansion<T, 8> abt = ExpansionBase<T>::TwoTwoDiffZeroCheck(adxtail, bdy, adytail, bdx) + ExpansionBase<T>::TwoTwoDiffZeroCheck(bdytail, adx, bdxtail, ady);
			Expansion<T, 192> fin2 = fin1 + bct * adz + cat * bdz + abt * cdz + bc * adztail + ca * bdztail + ab * cdztail
			                       + ExpansionBase<T>::ThreeProd(adxtail, bdytail, cdz) + ExpansionBase<T>::ThreeProd(adxtail, bdytail, cdztail)
			                       + ExpansionBase<T>::ThreeProd(-adxtail, cdytail, bdz) + ExpansionBase<T>::ThreeProd(-adxtail, cdytail, bdztail)
			                       + ExpansionBase<T>::ThreeProd(bdxtail, cdytail, adz) + ExpansionBase<T>::ThreeProd(bdxtail, cdytail, adztail)
			                       + ExpansionBase<T>::ThreeProd(-bdxtail, adytail, cdz) + ExpansionBase<T>::ThreeProd(-bdxtail, adytail, cdztail)
			                       + ExpansionBase<T>::ThreeProd(cdxtail, adytail, bdz) + ExpansionBase<T>::ThreeProd(cdxtail, adytail, bdztail)
			                       + ExpansionBase<T>::ThreeProd(-cdxtail, bdytail, adz) + ExpansionBase<T>::ThreeProd(-cdxtail, bdytail, adztail)
			                       + bct * adztail + cat * bdztail + abt * cdztail;
			return fin2.mostSignificant();
		}

		template <typename T> T insphere(const T* pa, const T* pb, const T* pc, const T* pd, const T* pe) {
			T permanent;
			const T aex = pa[0] - pe[0];
			const T bex = pb[0] - pe[0];
			const T cex = pc[0] - pe[0];
			const T dex = pd[0] - pe[0];
			const T aey = pa[1] - pe[1];
			const T bey = pb[1] - pe[1];
			const T cey = pc[1] - pe[1];
			const T dey = pd[1] - pe[1];
			const T aez = pa[2] - pe[2];
			const T bez = pb[2] - pe[2];
			const T cez = pc[2] - pe[2];
			const T dez = pd[2] - pe[2];
			{
				const T aexbey = aex * bey;
				const T bexaey = bex * aey;
				const T bexcey = bex * cey;
				const T cexbey = cex * bey;
				const T cexdey = cex * dey;
				const T dexcey = dex * cey;
				const T dexaey = dex * aey;
				const T aexdey = aex * dey;
				const T aexcey = aex * cey;
				const T cexaey = cex * aey;
				const T bexdey = bex * dey;
				const T dexbey = dex * bey;
				const T ab = aexbey - bexaey;
				const T bc = bexcey - cexbey;
				const T cd = cexdey - dexcey;
				const T da = dexaey - aexdey;
				const T ac = aexcey - cexaey;
				const T bd = bexdey - dexbey;
				const T abc = aez * bc - bez * ac + cez * ab;
				const T bcd = bez * cd - cez * bd + dez * bc;
				const T cda = cez * da + dez * ac + aez * cd;
				const T dab = dez * ab + aez * bd + bez * da;
				const T alift = aex * aex + aey * aey + aez * aez;
				const T blift = bex * bex + bey * bey + bez * bez;
				const T clift = cex * cex + cey * cey + cez * cez;
				const T dlift = dex * dex + dey * dey + dez * dez;
				const T det = (dlift * abc - clift * dab) + (blift * cda - alift * bcd);
				const T aezplus = std::abs(aez);
				const T bezplus = std::abs(bez);
				const T cezplus = std::abs(cez);
				const T dezplus = std::abs(dez);
				const T aexbeyplus = std::abs(aexbey);
				const T bexaeyplus = std::abs(bexaey);
				const T bexceyplus = std::abs(bexcey);
				const T cexbeyplus = std::abs(cexbey);
				const T cexdeyplus = std::abs(cexdey);
				const T dexceyplus = std::abs(dexcey);
				const T dexaeyplus = std::abs(dexaey);
				const T aexdeyplus = std::abs(aexdey);
				const T aexceyplus = std::abs(aexcey);
				const T cexaeyplus = std::abs(cexaey);
				const T bexdeyplus = std::abs(bexdey);
				const T dexbeyplus = std::abs(dexbey);
				permanent = ((cexdeyplus + dexceyplus) * bezplus + (dexbeyplus + bexdeyplus) * cezplus + (bexceyplus + cexbeyplus) * dezplus) * alift
				          + ((dexaeyplus + aexdeyplus) * cezplus + (aexceyplus + cexaeyplus) * dezplus + (cexdeyplus + dexceyplus) * aezplus) * blift
				          + ((aexbeyplus + bexaeyplus) * dezplus + (bexdeyplus + dexbeyplus) * aezplus + (dexaeyplus + aexdeyplus) * bezplus) * clift
				          + ((bexceyplus + cexbeyplus) * aezplus + (cexaeyplus + aexceyplus) * bezplus + (aexbeyplus + bexaeyplus) * cezplus) * dlift;
				const T errbound = Constants<T>::isperrboundA * permanent;
				if(std::abs(det) >= std::abs(errbound)) return det;
			}

			const Expansion<T, 4> ab = ExpansionBase<T>::TwoTwoDiff(aex, bey, bex, aey);
			const Expansion<T, 4> bc = ExpansionBase<T>::TwoTwoDiff(bex, cey, cex, bey);
			const Expansion<T, 4> cd = ExpansionBase<T>::TwoTwoDiff(cex, dey, dex, cey);
			const Expansion<T, 4> da = ExpansionBase<T>::TwoTwoDiff(dex, aey, aex, dey);
			const Expansion<T, 4> ac = ExpansionBase<T>::TwoTwoDiff(aex, cey, cex, aey);
			const Expansion<T, 4> bd = ExpansionBase<T>::TwoTwoDiff(bex, dey, dex, bey);
			const Expansion<T, 24> temp24a = bc * dez + (cd * bez + bd * -cez);
			const Expansion<T, 24> temp24b = cd * aez + (da * cez + ac *  dez);
			const Expansion<T, 24> temp24c = da * bez + (ab * dez + bd *  aez);
			const Expansion<T, 24> temp24d = ab * cez + (bc * aez + ac * -bez);
			const Expansion<T, 288> adet = temp24a * aex * -aex + temp24a * aey * -aey + temp24a * aez * -aez;
			const Expansion<T, 288> bdet = temp24b * bex *  bex + temp24b * bey *  bey + temp24b * bez *  bez;
			const Expansion<T, 288> cdet = temp24c * cex * -cex + temp24c * cey * -cey + temp24c * cez * -cez;
			const Expansion<T, 288> ddet = temp24d * dex *  dex + temp24d * dey *  dey + temp24d * dez *  dez;
			const Expansion<T, 1152> fin1 = (adet + bdet) + (cdet + ddet);
			T det = fin1.estimate();
			T errbound = Constants<T>::isperrboundB * permanent;
			if(std::abs(det) >= std::abs(errbound)) return det;

			const T aextail = ExpansionBase<T>::MinusTail(pa[0], pe[0], aex);
			const T aeytail = ExpansionBase<T>::MinusTail(pa[1], pe[1], aey);
			const T aeztail = ExpansionBase<T>::MinusTail(pa[2], pe[2], aez);
			const T bextail = ExpansionBase<T>::MinusTail(pb[0], pe[0], bex);
			const T beytail = ExpansionBase<T>::MinusTail(pb[1], pe[1], bey);
			const T beztail = ExpansionBase<T>::MinusTail(pb[2], pe[2], bez);
			const T cextail = ExpansionBase<T>::MinusTail(pc[0], pe[0], cex);
			const T ceytail = ExpansionBase<T>::MinusTail(pc[1], pe[1], cey);
			const T ceztail = ExpansionBase<T>::MinusTail(pc[2], pe[2], cez);
			const T dextail = ExpansionBase<T>::MinusTail(pd[0], pe[0], dex);
			const T deytail = ExpansionBase<T>::MinusTail(pd[1], pe[1], dey);
			const T deztail = ExpansionBase<T>::MinusTail(pd[2], pe[2], dez);
			if (T(0) == aextail && T(0) == aeytail && T(0) == aeztail &&
			    T(0) == bextail && T(0) == beytail && T(0) == beztail &&
			    T(0) == cextail && T(0) == ceytail && T(0) == ceztail &&
			    T(0) == dextail && T(0) == deytail && T(0) == deztail) return det;

			errbound = Constants<T>::isperrboundC * permanent + Constants<T>::resulterrbound * std::abs(det);
			const T abeps = (aex * beytail + bey * aextail) - (aey * bextail + bex * aeytail);
			const T bceps = (bex * ceytail + cey * bextail) - (bey * cextail + cex * beytail);
			const T cdeps = (cex * deytail + dey * cextail) - (cey * dextail + dex * ceytail);
			const T daeps = (dex * aeytail + aey * dextail) - (dey * aextail + aex * deytail);
			const T aceps = (aex * ceytail + cey * aextail) - (aey * cextail + cex * aeytail);
			const T bdeps = (bex * deytail + dey * bextail) - (bey * dextail + dex * beytail);
			const T ab3 = ab.mostSignificant();
			const T bc3 = bc.mostSignificant();
			const T cd3 = cd.mostSignificant();
			const T da3 = da.mostSignificant();
			const T ac3 = ac.mostSignificant();
			const T bd3 = bd.mostSignificant();
			det += ( ( (bex * bex + bey * bey + bez * bez) * ((cez * daeps + dez * aceps + aez * cdeps) + (ceztail * da3 + deztail * ac3 + aeztail * cd3))
			         + (dex * dex + dey * dey + dez * dez) * ((aez * bceps - bez * aceps + cez * abeps) + (aeztail * bc3 - beztail * ac3 + ceztail * ab3)) )
			       - ( (aex * aex + aey * aey + aez * aez) * ((bez * cdeps - cez * bdeps + dez * bceps) + (beztail * cd3 - ceztail * bd3 + deztail * bc3))
			         + (cex * cex + cey * cey + cez * cez) * ((dez * abeps + aez * bdeps + bez * daeps) + (deztail * ab3 + aeztail * bd3 + beztail * da3)) ) )
			    + T(2) * ( ( (bex * bextail + bey * beytail + bez * beztail) * (cez * da3 + dez * ac3 + aez * cd3)
			               + (dex * dextail + dey * deytail + dez * deztail) * (aez * bc3 - bez * ac3 + cez * ab3))
			             - ( (aex * aextail + aey * aeytail + aez * aeztail) * (bez * cd3 - cez * bd3 + dez * bc3)
			               + (cex * cextail + cey * ceytail + cez * ceztail) * (dez * ab3 + aez * bd3 + bez * da3)));
			if(std::abs(det) >= std::abs(errbound)) return det;
			return exact::insphere(pa, pb, pc, pd, pe);
		}
	}
}
