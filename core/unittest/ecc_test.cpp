// Copyright 2018 The Beam Team
// Copyright 2019 - 2022 The LiteCash Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include "../ecc_native.h"
#include "../block_crypt.h"
#include "../treasury.h"
#include "../../utility/serialize.h"
#include "../serialization_adapters.h"
#include "../aes.h"
#include "../proto.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic ignored "-Wunused-result"
#endif

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706 4701)
#endif

#include "secp256k1-zkp/include/secp256k1_rangeproof.h" // For benchmark comparison with secp256k1

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706 4701)
#	pragma warning (pop)
#endif

void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a);
secp256k1_context* g_psecp256k1 = NULL;

int g_TestsFailed = 0;

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

namespace ECC {

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*) p)[i] = (uint8_t) rand();
}

void SetRandom(uintBig& x)
{
	GenerateRandom(x.m_pData, x.nBytes);
}

void SetRandom(Scalar::Native& x)
{
	Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

template <typename T>
void SetRandomOrd(T& x)
{
	GenerateRandom(&x, sizeof(x));
}

uint32_t get_LsBit(const uint8_t* pSrc, uint32_t nSrc, uint32_t iBit)
{
	uint32_t iByte = iBit >> 3;
	if (iByte >= nSrc)
		return 0;

	return 1 & (pSrc[nSrc - 1 - iByte] >> (7 & iBit));
}

void TestShifted2(const uint8_t* pSrc, uint32_t nSrc, const uint8_t* pDst, uint32_t nDst, int nShift)
{
	for (uint32_t iBitDst = 0; iBitDst < (nDst << 3); iBitDst++)
	{
		uint32_t a = get_LsBit(pSrc, nSrc, iBitDst - nShift);
		uint32_t b = get_LsBit(pDst, nDst, iBitDst);
		verify_test(a == b);
	}
}

template <uint32_t n0, uint32_t n1>
void TestShifted(const beam::uintBig_t<n0>& x0, const beam::uintBig_t<n1>& x1, int nShift)
{
	TestShifted2(x0.m_pData, x0.nBytes, x1.m_pData, x1.nBytes, nShift);
}

template <uint32_t n0, uint32_t n1>
void TestShifts(const beam::uintBig_t<n0>& src, beam::uintBig_t<n0>& src2, beam::uintBig_t<n1>& trg, int nShift)
{
	src2 = src;
	src2.ShiftLeft(nShift, trg);
	TestShifted(src, trg, nShift);
	src2 = src;
	src2.ShiftRight(nShift, trg);
	TestShifted(src, trg, -nShift);
}

void TestUintBig()
{
	for (int i = 0; i < 100; i++)
	{
		uint32_t a, b;
		SetRandomOrd(a);
		SetRandomOrd(b);

		uint64_t ab = a;
		ab *= b;

		uintBig v0, v1;
		v0 = a;
		v1 = b;
		v0 = v0 * v1;
		v1 = ab;

		verify_test(v0 == v1);

		ab = a;
		ab += b;

		v0 = a;
		v1 = b;

		v0 += v1;
		v1 = ab;

		verify_test(v0 == v1);
	}

	// test shifts, when src/dst types is smaller/bigger/equal
	for (int j = 0; j < 20; j++)
	{
		beam::uintBig_t<32> a;
		beam::uintBig_t<32 - 8> b;
		beam::uintBig_t<32 + 8> c;
		beam::uintBig_t<32> d;

		SetRandom(a);

		for (int i = 0; i < 512; i++)
		{
			TestShifts(a, a, b, i);
			TestShifts(a, a, c, i);
			TestShifts(a, a, d, i);
			TestShifts(a, d, d, i); // inplace shift
		}
	}
}

void TestHash()
{
	Oracle oracle;
	Hash::Value hv;
	oracle >> hv;

	for (int i = 0; i < 10; i++)
	{
		Hash::Value hv2 = hv;
		oracle >> hv;

		// hash values must change, even if no explicit input was fed.
		verify_test(!(hv == hv2));
	}
}

void TestScalars()
{
	Scalar::Native s0, s1, s2;
	s0 = 17U;

	// neg
	s1 = -s0;
	verify_test(!(s1 == Zero));
	s1 += s0;
	verify_test(s1 == Zero);

	// inv, mul
	s1.SetInv(s0);

	s2 = -s1;
	s2 += s0;
	verify_test(!(s2 == Zero));

	s1 *= s0;
	s2 = 1U;
	s2 = -s2;
	s2 += s1;
	verify_test(s2 == Zero);

	// import,export

	for (int i = 0; i < 1000; i++)
	{
		SetRandom(s0);

		Scalar s_(s0);
		s1 = s_;
		verify_test(s0 == s1);

		s1 = -s1;
		s1 += s0;
		verify_test(s1 == Zero);
	}

	// powers
	ScalarGenerator pwrGen, pwrGenInv;
	s0 = 7U; // looks like a good generator
	pwrGen.Initialize(s0);
	s0.Inv();
	pwrGenInv.Initialize(s0);


	for (int i = 0; i < 20; i++)
	{
		Scalar pwr;
		SetRandom(pwr.m_Value); // don't care if overflows, just doesn't matter

		pwrGen.Calculate(s1, pwr);
		pwrGenInv.Calculate(s2, pwr);

		s0.SetInv(s2);
		verify_test(s0 == s1);
	}
}

void TestPoints()
{
	Mode::Scope scope(Mode::Fast); // suppress assertion in point multiplication

	// generate, import, export
	Point::Native p0, p1;
	Point p_, p2_;

	p_.m_X = Zero; // should be zero-point
	p_.m_Y = 1;
	verify_test(!p0.Import(p_));
	verify_test(p0 == Zero);

	p_.m_Y = 0;
	verify_test(p0.Import(p_));
	verify_test(p0 == Zero);

	p2_ = p0;
	verify_test(p_ == p2_);

	for (int i = 0; i < 1000; i++)
	{
		SetRandom(p_.m_X);
		p_.m_Y = (1 & i);

		while (!p0.Import(p_))
		{
			verify_test(p0 == Zero);
			p_.m_X.Inc();
		}
		verify_test(!(p0 == Zero));

		p1 = -p0;
		verify_test(!(p1 == Zero));

		p1 += p0;
		verify_test(p1 == Zero);

		p2_ = p0;
		verify_test(p_ == p2_);
	}

	// multiplication
	Scalar::Native s0, s1;

	s0 = 1U;

	Point::Native g = Context::get().G * s0;
	verify_test(!(g == Zero));

	s0 = Zero;
	p0 = Context::get().G * s0;
	verify_test(p0 == Zero);

	p0 += g * s0;
	verify_test(p0 == Zero);

	for (int i = 0; i < 300; i++)
	{
		SetRandom(s0);

		p0 = Context::get().G * s0; // via generator

		s1 = -s0;
		p1 = p0;
		p1 += Context::get().G * s1; // inverse, also testing +=
		verify_test(p1 == Zero);

		p1 = p0;
		p1 += g * s1; // simple multiplication

		verify_test(p1 == Zero);
	}

	// H-gen
	Point::Native h = Context::get().H * 1U;
	verify_test(!(h == Zero));

	p0 = Context::get().H * 0U;
	verify_test(p0 == Zero);

	for (int i = 0; i < 300; i++)
	{
		Amount val;
		SetRandomOrd(val);

		p0 = Context::get().H * val; // via generator

		s0 = val;

		p1 = Zero;
		p1 += h * s0;
		p1 = -p1;
		p1 += p0;

		verify_test(p1 == Zero);
	}

	// doubling, all bits test
	s0 = 1U;
	s1 = 2U;
	p0 = g;

	for (int nBit = 1; nBit < 256; nBit++)
	{
		s0 *= s1;
		p1 = Context::get().G * s0;
		verify_test(!(p1 == Zero));

		p0 = p0 * Two;
		p0 = -p0;
		p0 += p1;
		verify_test(p0 == Zero);

		p0 = p1;
	}

	SetRandom(s1);
	p0 = g * s1;

	{
		Mode::Scope scope2(Mode::Secure);
		p1 = g * s1;
	}

	p1 = -p1;
	p1 += p0;
	verify_test(p1 == Zero);

	// Make sure we use the same G-generator as in secp256k1
	SetRandom(s0);
	secp256k1_ecmult_gen(g_psecp256k1, &p0.get_Raw(), &s0.get());
	p1 = Context::get().G * s0;

	p1 = -p1;
	p1 += p0;
	verify_test(p1 == Zero);
}

void TestSigning()
{
	for (int i = 0; i < 30; i++)
	{
		Scalar::Native sk; // private key
		SetRandom(sk);

		Point::Native pk; // public key
		pk = Context::get().G * sk;

		Signature mysig;

		uintBig msg;
		SetRandom(msg); // assumed message

		mysig.Sign(msg, sk);

		verify_test(mysig.IsValid(msg, pk));

		// tamper msg
		uintBig msg2 = msg;
		msg2.Inc();

		verify_test(!mysig.IsValid(msg2, pk));

		// try to sign with different key
		Scalar::Native sk2;
		SetRandom(sk2);

		Signature mysig2;
		mysig2.Sign(msg, sk2);
		verify_test(!mysig2.IsValid(msg, pk));

		// tamper signature
		mysig2 = mysig;
		mysig2.m_NoncePub.m_Y = !mysig2.m_NoncePub.m_Y;
		verify_test(!mysig2.IsValid(msg, pk));

		mysig2 = mysig;
		SetRandom(mysig2.m_k.m_Value);
		verify_test(!mysig2.IsValid(msg, pk));
	}
}

void TestCommitments()
{
	Scalar::Native kExcess(Zero);

	Amount vSum = 0;

	Point::Native commInp(Zero);

	// inputs
	for (uint32_t i = 0; i < 7; i++)
	{
		Amount v = (i+50) * 400;
		Scalar::Native sk;
		SetRandom(sk);

		commInp += Commitment(sk, v);

		kExcess += sk;
		vSum += v;
	}

	// single output
	Point::Native commOutp(Zero);
	{
		Scalar::Native sk;
		SetRandom(sk);

		commOutp += Commitment(sk, vSum);

		sk = -sk;
		kExcess += sk;
	}

	Point::Native sigma = Context::get().G * kExcess;
	sigma += commOutp;

	sigma = -sigma;
	sigma += commInp;

	verify_test(sigma == Zero);

	// switch commitment
	HKdf kdf;
	uintBig seed;
	SetRandom(seed);
	kdf.Generate(seed);

	Key::IDV kidv(100500, 15, Key::Type::Regular, 7);

	Scalar::Native sk;
	ECC::Point::Native comm;
	beam::SwitchCommitment().Create(sk, comm, kdf, kidv);

	sigma = Commitment(sk, kidv.m_Value);
	sigma = -sigma;
	sigma += comm;
	verify_test(sigma == Zero);

	beam::SwitchCommitment().Recover(sigma, kdf, kidv);
	sigma = -sigma;
	sigma += comm;
	verify_test(sigma == Zero);
}

template <typename T>
void WriteSizeSerialized(const char* sz, const T& t)
{
	beam::SerializerSizeCounter ssc;
	ssc & t;

	printf("%s size = %u\n", sz, (uint32_t) ssc.m_Counter.m_Value);
}

struct AssetTag
{
	Point::Native m_hGen;
	void Commit(Point::Native& out, const Scalar::Native& sk, Amount v)
	{
		out = Context::get().G * sk;
		Tag::AddValue(out, &m_hGen, v);
	}
};

void TestRangeProof(bool bCustomTag)
{
	RangeProof::CreatorParams cp;
	SetRandomOrd(cp.m_Kidv.m_Idx);
	SetRandomOrd(cp.m_Kidv.m_Type);
	SetRandomOrd(cp.m_Kidv.m_SubIdx);
	SetRandom(cp.m_Seed.V);
	cp.m_Kidv.m_Value = 345000;

	beam::AssetID aid;
	if (bCustomTag)
		SetRandom(aid);
	else
		aid = Zero;

	AssetTag tag;
	tag.m_hGen = beam::SwitchCommitment(&aid).m_hGen;

	Scalar::Native sk;
	SetRandom(sk);

	RangeProof::Public rp;
	{
		Oracle oracle;
		rp.Create(sk, cp, oracle);
		verify_test(rp.m_Value == cp.m_Kidv.m_Value);
	}

	Point::Native comm;
	tag.Commit(comm, sk, rp.m_Value);

	{
		Oracle oracle;
		verify_test(rp.IsValid(comm, oracle, &tag.m_hGen));
	}

	{
		RangeProof::CreatorParams cp2;
		cp2.m_Seed = cp.m_Seed;

		rp.Recover(cp2);
		verify_test(cp.m_Kidv == cp2.m_Kidv);
	}

	// tamper value
	rp.m_Value++;
	{
		Oracle oracle;
		verify_test(!rp.IsValid(comm, oracle, &tag.m_hGen));
	}
	rp.m_Value--;

	// try with invalid key
	SetRandom(sk);

	tag.Commit(comm, sk, rp.m_Value);

	{
		Oracle oracle;
		verify_test(!rp.IsValid(comm, oracle, &tag.m_hGen));
	}

	Scalar::Native pA[InnerProduct::nDim];
	Scalar::Native pB[InnerProduct::nDim];

	for (size_t i = 0; i < _countof(pA); i++)
	{
		SetRandom(pA[i]);
		SetRandom(pB[i]);
	}

	Scalar::Native pwrMul, dot;

	InnerProduct::get_Dot(dot, pA, pB);

	SetRandom(pwrMul);
	InnerProduct::Modifier mod;
	mod.m_pMultiplier[1] = &pwrMul;

	InnerProduct sig;
	sig.Create(comm, dot, pA, pB, mod);

	InnerProduct::get_Dot(dot, pA, pB);

	verify_test(sig.IsValid(comm, dot, mod));

	RangeProof::Confidential bp;
	cp.m_Kidv.m_Value = 23110;

	tag.Commit(comm, sk, cp.m_Kidv.m_Value);

	{
		Oracle oracle;
		bp.Create(sk, cp, oracle, &tag.m_hGen);
	}
	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, &tag.m_hGen));
	}
	{
		Oracle oracle;
		RangeProof::CreatorParams cp2;
		cp2.m_Seed = cp.m_Seed;

		bp.Recover(oracle, cp2);
		verify_test(cp.m_Kidv == cp2.m_Kidv);
	}

	InnerProduct::BatchContextEx<2> bc;
	bc.m_bEnableBatch = true;

	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, bc, &tag.m_hGen)); // add to batch
	}

	SetRandom(sk);
	cp.m_Kidv.m_Value = 7223110;
	SetRandom(cp.m_Seed.V); // another seed for this bulletproof
	tag.Commit(comm, sk, cp.m_Kidv.m_Value);

	{
		Oracle oracle;
		bp.Create(sk, cp, oracle, &tag.m_hGen);
	}
	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, bc, &tag.m_hGen)); // add to batch
	}

	verify_test(bc.Flush()); // verify at once


	WriteSizeSerialized("BulletProof", bp);

	{
		// multi-signed bulletproof
		const uint32_t nSigners = 5;

		Scalar::Native pSk[nSigners];
		uintBig pSeed[nSigners];

		// 1st cycle. peers produce Part2
		RangeProof::Confidential::Part2 p2;
		ZeroObject(p2);

		RangeProof::Confidential::MultiSig msig;

		for (uint32_t i = 0; i < nSigners; i++)
		{
			SetRandom(pSk[i]);
			SetRandom(pSeed[i]);

			if (i + 1 < nSigners)
				verify_test(RangeProof::Confidential::MultiSig::CoSignPart(pSeed[i], p2)); // p2 aggregation
			else
			{
				Oracle oracle;
				bp.m_Part2 = p2;
				verify_test(bp.CoSign(pSeed[i], pSk[i], cp, oracle, RangeProof::Confidential::Phase::Step2, &msig, &tag.m_hGen)); // add last p2, produce msig
				p2 = bp.m_Part2;
			}
		}

		// 2nd cycle. Peers produce Part3, commitment is aggregated too
		RangeProof::Confidential::Part3 p3;
		ZeroObject(p3);

		comm = Zero;
		Tag::AddValue(comm, &tag.m_hGen, cp.m_Kidv.m_Value);

		for (uint32_t i = 0; i < nSigners; i++)
		{
			comm += Context::get().G * pSk[i];

			if (i + 1 < nSigners)
				msig.CoSignPart(pSeed[i], pSk[i], p3);
			else
			{
				Oracle oracle;
				bp.m_Part2 = p2;
				bp.m_Part3 = p3;
				verify_test(bp.CoSign(pSeed[i], pSk[i], cp, oracle, RangeProof::Confidential::Phase::Finalize, nullptr, &tag.m_hGen));
			}
		}


		{
			// test
			Oracle oracle;
			verify_test(bp.IsValid(comm, oracle, &tag.m_hGen));
		}
	}

	HKdf kdf;
	uintBig seed;
	SetRandom(seed);
	kdf.Generate(seed);

	{
		beam::Output outp;
		outp.m_AssetID = aid;
		outp.Create(sk, kdf, Key::IDV(20300, 1, Key::Type::Regular), kdf, true);
		outp.m_Coinbase = true; // others may be disallowed
		verify_test(outp.IsValid(comm));
		WriteSizeSerialized("Out-UTXO-Public", outp);
	}
	{
		beam::Output outp;
		outp.m_AssetID = aid;
		outp.Create(sk, kdf, Key::IDV(20300, 1, Key::Type::Regular), kdf);
		verify_test(outp.IsValid(comm));
		WriteSizeSerialized("Out-UTXO-Confidential", outp);
	}

	WriteSizeSerialized("In-Utxo", beam::Input());

	beam::TxKernel txk;
	txk.m_Fee = 50;
	WriteSizeSerialized("Kernel(simple)", txk);
}

struct TransactionMaker
{
	beam::Transaction m_Trans;
	HKdf m_Kdf;

	TransactionMaker()
	{
		m_Trans.m_Offset.m_Value = Zero;
	}

	struct Peer
	{
		Scalar::Native m_k;

		Peer()
		{
			m_k = Zero;
		}

		void FinalizeExcess(Point::Native& kG, Scalar::Native& kOffset)
		{
			kOffset += m_k;

			SetRandom(m_k);
			kOffset += m_k;

			m_k = -m_k;
			kG += Context::get().G * m_k;
		}

		void AddInput(beam::Transaction& t, Amount val, Key::IKdf& kdf, const beam::AssetID* pAssetID = nullptr)
		{
			std::unique_ptr<beam::Input> pInp(new beam::Input);

			Key::IDV kidv;
			SetRandomOrd(kidv.m_Idx);
			kidv.m_Type = Key::Type::Regular;
			kidv.m_SubIdx = 0;
			kidv.m_Value = val;

			Scalar::Native k;
			beam::SwitchCommitment(pAssetID).Create(k, pInp->m_Commitment, kdf, kidv);

			t.m_vInputs.push_back(std::move(pInp));
			m_k += k;
		}

		void AddOutput(beam::Transaction& t, Amount val, Key::IKdf& kdf, const beam::AssetID* pAssetID = nullptr)
		{
			std::unique_ptr<beam::Output> pOut(new beam::Output);

			Scalar::Native k;

			Key::IDV kidv;
			SetRandomOrd(kidv.m_Idx);
			kidv.m_Type = Key::Type::Regular;
			kidv.m_SubIdx = 0;
			kidv.m_Value = val;

			if (pAssetID)
				pOut->m_AssetID = *pAssetID;
			pOut->Create(k, kdf, kidv, kdf);

			// test recovery
			Key::IDV kidv2;
			verify_test(pOut->Recover(kdf, kidv2));
			verify_test(kidv == kidv2);

			t.m_vOutputs.push_back(std::move(pOut));

			k = -k;
			m_k += k;
		}

	};

	Peer m_pPeers[2]; // actually can be more

	void CoSignKernel(beam::TxKernel& krn, const Hash::Value& hvLockImage)
	{
		// 1st pass. Public excesses and Nonces are summed.
		Scalar::Native pX[_countof(m_pPeers)];
		Scalar::Native offset(m_Trans.m_Offset);

		Point::Native xG(Zero), kG(Zero);

		for (size_t i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];
			p.FinalizeExcess(kG, offset);

			SetRandom(pX[i]);
			xG += Context::get().G * pX[i];
		}

		m_Trans.m_Offset = offset;

		for (size_t i = 0; i < krn.m_vNested.size(); i++)
		{
			Point::Native ptNested;
			verify_test(ptNested.Import(krn.m_vNested[i]->m_Commitment));
			kG += Point::Native(ptNested);
		}

		krn.m_Commitment = kG;

		Hash::Value msg;
		krn.get_ID(msg, &hvLockImage);

		// 2nd pass. Signing. Total excess is the signature public key.
		Scalar::Native kSig = Zero;

		for (size_t i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];

			Signature::MultiSig msig;
			msig.m_Nonce = pX[i];
			msig.m_NoncePub = xG;

			Scalar::Native k;
			msig.SignPartial(k, msg, p.m_k);

			kSig += k;

			p.m_k = Zero; // signed, prepare for next tx
		}

		krn.m_Signature.m_NoncePub = xG;
		krn.m_Signature.m_k = kSig;
	}

	void CreateTxKernel(std::vector<beam::TxKernel::Ptr>& lstTrg, Amount fee, std::vector<beam::TxKernel::Ptr>& lstNested, bool bEmitCustomTag)
	{
		std::unique_ptr<beam::TxKernel> pKrn(new beam::TxKernel);
		pKrn->m_Fee = fee;

		pKrn->m_vNested.swap(lstNested);

		// hashlock
		pKrn->m_pHashLock.reset(new beam::TxKernel::HashLock);

		uintBig hlPreimage;
		SetRandom(hlPreimage);

		Hash::Value hvLockImage;
		Hash::Processor() << hlPreimage >> hvLockImage;

		if (bEmitCustomTag)
		{
			// emit some asset
			Scalar::Native skAsset;
			beam::AssetID aid;
			Amount valAsset = 4431;

			SetRandom(skAsset);
			beam::proto::Sk2Pk(aid, skAsset);

			if (beam::Rules::get().CA.Deposit)
				m_pPeers[0].AddInput(m_Trans, valAsset, m_Kdf); // input being-deposited

			m_pPeers[0].AddOutput(m_Trans, valAsset, m_Kdf, &aid); // output UTXO to consume the created asset

			std::unique_ptr<beam::TxKernel> pKrnEmission(new beam::TxKernel);
			pKrnEmission->m_AssetEmission = valAsset;
			pKrnEmission->m_Commitment.m_X = aid;
			pKrnEmission->m_Commitment.m_Y = 0;
			pKrnEmission->Sign(skAsset);

			lstTrg.push_back(std::move(pKrnEmission));

			skAsset = -skAsset;
			m_pPeers[0].m_k += skAsset;
		}

		CoSignKernel(*pKrn, hvLockImage);


		Point::Native exc;
		beam::AmountBig::Type fee2;
		verify_test(!pKrn->IsValid(fee2, exc)); // should not pass validation unless correct hash preimage is specified

		// finish HL: add hash preimage
		pKrn->m_pHashLock->m_Preimage = hlPreimage;
		verify_test(pKrn->IsValid(fee2, exc));

		lstTrg.push_back(std::move(pKrn));
	}

	void AddInput(int i, Amount val)
	{
		m_pPeers[i].AddInput(m_Trans, val, m_Kdf);
	}

	void AddOutput(int i, Amount val)
	{
		m_pPeers[i].AddOutput(m_Trans, val, m_Kdf);
	}
};

void TestTransaction()
{
	TransactionMaker tm;
	tm.AddInput(0, 3000);
	tm.AddInput(0, 2000);
	tm.AddOutput(0, 500);

	tm.AddInput(1, 1000);
	tm.AddOutput(1, 5400);

	std::vector<beam::TxKernel::Ptr> lstNested, lstDummy;

	Amount fee1 = 100, fee2 = 2;

	tm.CreateTxKernel(lstNested, fee1, lstDummy, false);

	tm.AddOutput(0, 738);
	tm.AddInput(1, 740);
	tm.CreateTxKernel(tm.m_Trans.m_vKernels, fee2, lstNested, true);

	tm.m_Trans.Normalize();

	beam::TxBase::Context::Params pars;
	beam::TxBase::Context ctx(pars);
	verify_test(tm.m_Trans.IsValid(ctx));
	verify_test(ctx.m_Fee == beam::AmountBig::Type(fee1 + fee2));
}

void TestCutThrough()
{
	TransactionMaker tm;
	tm.AddOutput(0, 3000);
	tm.AddOutput(0, 2000);

	tm.m_Trans.Normalize();

	beam::TxBase::Context::Params pars;
	beam::TxBase::Context ctx(pars);
	verify_test(ctx.ValidateAndSummarize(tm.m_Trans, tm.m_Trans.get_Reader()));

	beam::Input::Ptr pInp(new beam::Input);
	pInp->m_Commitment = tm.m_Trans.m_vOutputs.front()->m_Commitment;
	tm.m_Trans.m_vInputs.push_back(std::move(pInp));

	ctx.Reset();
	verify_test(!ctx.ValidateAndSummarize(tm.m_Trans, tm.m_Trans.get_Reader())); // redundant outputs must be banned!

	verify_test(tm.m_Trans.Normalize() == 1);

	ctx.Reset();
	verify_test(ctx.ValidateAndSummarize(tm.m_Trans, tm.m_Trans.get_Reader()));
}

void TestAES()
{
	// AES in ECB mode (simplest): https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_Core256.pdf

	uint8_t pKey[AES::s_KeyBytes] = {
		0x60,0x3D,0xEB,0x10,0x15,0xCA,0x71,0xBE,0x2B,0x73,0xAE,0xF0,0x85,0x7D,0x77,0x81,
		0x1F,0x35,0x2C,0x07,0x3B,0x61,0x08,0xD7,0x2D,0x98,0x10,0xA3,0x09,0x14,0xDF,0xF4
	};

	const uint8_t pPlaintext[AES::s_BlockSize] = {
		0x6B,0xC1,0xBE,0xE2,0x2E,0x40,0x9F,0x96,0xE9,0x3D,0x7E,0x11,0x73,0x93,0x17,0x2A
	};

	const uint8_t pCiphertext[AES::s_BlockSize] = {
		0xF3,0xEE,0xD1,0xBD,0xB5,0xD2,0xA0,0x3C,0x06,0x4B,0x5A,0x7E,0x3D,0xB1,0x81,0xF8
	};

	struct {
		uint32_t zero0 = 0;
		AES::Encoder enc;
		uint32_t zero1 = 0;
	} se;

	se.enc.Init(pKey);
	verify_test(!se.zero0 && !se.zero1);

	uint8_t pBuf[sizeof(pPlaintext)];
	memcpy(pBuf, pPlaintext, sizeof(pBuf));

	se.enc.Proceed(pBuf, pBuf); // inplace encode
	verify_test(!memcmp(pBuf, pCiphertext, sizeof(pBuf)));

	struct {
		uint32_t zero0 = 0;
		AES::Decoder dec;
		uint32_t zero1 = 0;
	} sd;

	sd.dec.Init(se.enc);
	verify_test(!sd.zero0 && !sd.zero1);

	sd.dec.Proceed(pBuf, pBuf); // inplace decode
	verify_test(!memcmp(pBuf, pPlaintext, sizeof(pPlaintext)));
}

void TestKdf()
{
	HKdf skdf;
	HKdfPub pkdf;

	uintBig seed;
	SetRandom(seed);

	skdf.Generate(seed);
	pkdf.GenerateFrom(skdf);

	for (uint32_t i = 0; i < 10; i++)
	{
		Hash::Value hv;
		Hash::Processor() << "test_kdf" << i >> hv;

		Scalar::Native sk0, sk1;
		skdf.DerivePKey(sk0, hv);
		pkdf.DerivePKey(sk1, hv);
		verify_test(Scalar(sk0) == Scalar(sk1));

		skdf.DeriveKey(sk0, hv);
		verify_test(Scalar(sk0) != Scalar(sk1));

		Point::Native pk0, pk1;
		skdf.DerivePKeyG(pk0, hv);
		pkdf.DerivePKeyG(pk1, hv);
		pk1 = -pk1;
		pk0 += pk1;
		verify_test(pk0 == Zero);

		skdf.DerivePKeyJ(pk0, hv);
		pkdf.DerivePKeyJ(pk1, hv);
		pk1 = -pk1;
		pk0 += pk1;
		verify_test(pk0 == Zero);
	}

	const std::string sPass("test password");

	beam::KeyString ks1;
	ks1.SetPassword(sPass);
	ks1.m_sMeta = "hello, World!";

	ks1.Export(skdf);
	HKdf skdf2;
	ks1.m_sMeta.clear();
	ks1.SetPassword(sPass);
	verify_test(ks1.Import(skdf2));

	verify_test(skdf2.IsSame(skdf));

	ks1.Export(pkdf);
	HKdfPub pkdf2;
	verify_test(ks1.Import(pkdf2));
	verify_test(pkdf2.IsSame(pkdf));

	seed.Inc();
	skdf2.Generate(seed);
	verify_test(!skdf2.IsSame(skdf));
}

void TestBbs()
{
	Scalar::Native privateAddr, nonce;
	beam::PeerID publicAddr;

	SetRandom(privateAddr);
	beam::proto::Sk2Pk(publicAddr, privateAddr);

	const char szMsg[] = "Hello, World!";

	SetRandom(nonce);
	beam::ByteBuffer buf;
	verify_test(beam::proto::Bbs::Encrypt(buf, publicAddr, nonce, szMsg, sizeof(szMsg)));

	uint8_t* p = &buf.at(0);
	uint32_t n = (uint32_t) buf.size();

	verify_test(beam::proto::Bbs::Decrypt(p, n, privateAddr));
	verify_test(n == sizeof(szMsg));
	verify_test(!memcmp(p, szMsg, n));

	SetRandom(privateAddr);
	p = &buf.at(0);
	n = (uint32_t) buf.size();

	verify_test(!beam::proto::Bbs::Decrypt(p, n, privateAddr));
}

void TestRatio(const beam::Difficulty& d0, const beam::Difficulty& d1, double k)
{
	const double tol = 1.000001;
	double k_ = d0.ToFloat() / d1.ToFloat();
	verify_test((k_ < k * tol) && (k < k_ * tol));
}

void TestDifficulty()
{
	using namespace beam;

	Difficulty::Raw r1, r2;
	Difficulty(Difficulty::s_Inf).Unpack(r1);
	Difficulty(Difficulty::s_Inf - 1).Unpack(r2);
	verify_test(r1 > r2);

	uintBig val(Zero);

	verify_test(Difficulty(Difficulty::s_Inf).IsTargetReached(val));

	val.m_pData[0] = 0x80; // msb set

	verify_test(Difficulty(0).IsTargetReached(val));
	verify_test(Difficulty(1).IsTargetReached(val));
	verify_test(Difficulty(0xffffff).IsTargetReached(val)); // difficulty almost 2
	verify_test(!Difficulty(0x1000000).IsTargetReached(val)); // difficulty == 2

	val.m_pData[0] = 0x7f;
	verify_test(Difficulty(0x1000000).IsTargetReached(val));

	// Adjustments
	Difficulty d, d2;
	d.m_Packed = 3 << Difficulty::s_MantissaBits;

	Difficulty::Raw raw, wrk;
	d.Unpack(raw);
	uint32_t dh = 1440;
	wrk.AssignMul(raw, uintBigFrom(dh));

	d2.Calculate(wrk, dh, 100500, 100500);
	TestRatio(d2, d, 1.);

	// slight increase
	d2.Calculate(wrk, dh, 100500, 100000);
	TestRatio(d2, d, 1.005);

	// strong increase
	d2.Calculate(wrk, dh, 180000, 100000);
	TestRatio(d2, d, 1.8);

	// huge increase
	d2.Calculate(wrk, dh, 7380000, 100000);
	TestRatio(d2, d, 73.8);

	// insane increase (1.7 billions). Still must fit
	d2.Calculate(wrk, dh, 1794380000, 1);
	TestRatio(d2, d, 1794380000);

	// slight decrease
	d2.Calculate(wrk, dh, 100000, 100500);
	TestRatio(d, d2, 1.005);

	// strong decrease
	d2.Calculate(wrk, dh, 100000, 180000);
	TestRatio(d, d2, 1.8);

	// insane decrease, out-of-bound
	d2.Calculate(wrk, dh, 100000, 7380000);
	verify_test(!d2.m_Packed);

	for (uint32_t i = 0; i < 200; i++)
	{
		GenerateRandom(&d, sizeof(d));

		uintBig trg;
		if (!d.get_Target(trg))
		{
			verify_test(d.m_Packed >= Difficulty::s_Inf);
			continue;
		}

		verify_test(d.IsTargetReached(trg));

		trg.Inc();
		if (!(trg == Zero)) // overflow?
			verify_test(!d.IsTargetReached(trg));
	}
}

void TestRandom()
{
	uintBig pV[2];
	ZeroObject(pV);

	for (uint32_t i = 0; i < 10; i++)
	{
		uintBig& a = pV[1 & i];
		uintBig& b = pV[1 & (i + 1)];

		a = Zero;
		GenRandom(a);
		verify_test(!(a == Zero));
		verify_test(!(a == b));
	}
}

bool IsOkFourCC(const char* szRes, const char* szSrc)
{
	// the formatted FourCC always consists of 4 characters. If source is shorter - spaced are appended
	size_t n = strlen(szSrc);
	for (size_t i = 0; i < 4; i++)
	{
		char c = (i < n) ? szSrc[i] : ' ';
		if (szRes[i] != c)
			return false;
	}

	return !szRes[4];

}

void TestFourCC()
{
#define TEST_FOURCC(name) \
	{ \
		uint32_t nFourCC = FOURCC_FROM(name); \
		beam::FourCC::Text txt(nFourCC); \
		verify_test(IsOkFourCC(txt, #name)); \
	}

	// compile-time FourCC should support shorter strings
	TEST_FOURCC(help)
	TEST_FOURCC(hel)
	TEST_FOURCC(he)
	TEST_FOURCC(h)
}

void TestTreasury()
{
	beam::Treasury::Parameters pars;
	pars.m_Bursts = 12;
	pars.m_MaturityStep = 1440 * 30 * 4;

	beam::Treasury tres;

	const uint32_t nPeers = 3;
	HKdf pKdfs[nPeers];

	for (uint32_t i = 0; i < nPeers; i++)
	{
		// 1. target wallet is initialized, generates its PeerID
		uintBig seed;
		SetRandom(seed);
		pKdfs[i].Generate(seed);

		beam::PeerID pid;
		Scalar::Native sk;
		beam::Treasury::get_ID(pKdfs[i], pid, sk);

		// 2. Plan is created (2%, 3%, 4% of the total emission)
		beam::Treasury::Entry* pE = tres.CreatePlan(pid, beam::Rules::get().Emission.Value0 * (i + 2)/100, pars);
		verify_test(pE->m_Request.m_WalletID == pid);

		// test Request serialization
		beam::Serializer ser0;
		ser0 & pE->m_Request;

		beam::Deserializer der0;
		der0.reset(ser0.buffer().first, ser0.buffer().second);

		beam::Treasury::Request req;
		der0 & req;

		// 3. Plan is appvoved by the wallet, response is generated
		pE->m_pResponse.reset(new beam::Treasury::Response);
		uint64_t nIndex = 1;
		verify_test(pE->m_pResponse->Create(req, pKdfs[i], nIndex));
		verify_test(pE->m_pResponse->m_WalletID == pid);

		// 4. Reponse is verified
		verify_test(pE->m_pResponse->IsValid(pE->m_Request));
	}

	// test serialization
	beam::Serializer ser1;
	ser1 & tres;

	tres.m_Entries.clear();

	beam::Deserializer der1;
	der1.reset(ser1.buffer().first, ser1.buffer().second);
	der1 & tres;

	verify_test(tres.m_Entries.size() == nPeers);

	std::string msg = "cool treasury";
	beam::Treasury::Data data;
	data.m_sCustomMsg = msg;
	tres.Build(data);
	verify_test(!data.m_vGroups.empty());

	std::vector<beam::Treasury::Data::Burst> vBursts = data.get_Bursts();

	// test serialization
	beam::ByteBuffer bb;
	ser1.swap_buf(bb);
	ser1 & data;

	data.m_vGroups.clear();
	data.m_sCustomMsg.clear();

	der1.reset(ser1.buffer().first, ser1.buffer().second);
	der1 & data;

	verify_test(!data.m_vGroups.empty());
	verify_test(data.m_sCustomMsg == msg);
	verify_test(data.IsValid());

	for (uint32_t i = 0; i < nPeers; i++)
	{
		std::vector<beam::Treasury::Data::Coin> vCoins;
		data.Recover(pKdfs[i], vCoins);
		verify_test(vCoins.size() == pars.m_Bursts);
	}
}

void TestAll()
{
	TestUintBig();
	TestHash();
	TestScalars();
	TestPoints();
	TestSigning();
	TestCommitments();
	TestRangeProof(false);
	TestRangeProof(true);
	TestTransaction();
	TestCutThrough();
	TestAES();
	TestKdf();
	TestBbs();
	TestDifficulty();
	TestRandom();
	TestFourCC();
	TestTreasury();
}


struct BenchmarkMeter
{
	const char* m_sz;

	uint64_t m_Start;
	uint64_t m_Cycles;

	uint32_t N;

#ifdef WIN32

	uint64_t m_Freq;

	static uint64_t get_Time()
	{
		uint64_t n;
		QueryPerformanceCounter((LARGE_INTEGER*) &n);
		return n;
	}

#else // WIN32

	static const uint64_t m_Freq = 1000000000;
	static uint64_t get_Time()
	{
		timespec tp;
		verify_test(!clock_gettime(CLOCK_MONOTONIC, &tp));
		return uint64_t(tp.tv_sec) * m_Freq + tp.tv_nsec;
	}

#endif // WIN32


	BenchmarkMeter(const char* sz)
		:m_sz(sz)
		,m_Cycles(0)
		,N(1000)
	{
#ifdef WIN32
		QueryPerformanceFrequency((LARGE_INTEGER*) &m_Freq);
#endif // WIN32

		m_Start = get_Time();
	}

	bool ShouldContinue()
	{
		m_Cycles += N;

		double dt_s = double(get_Time() - m_Start) / double(m_Freq);
		if (dt_s >= 1.)
		{
			printf("%-24s: %.2f us\n", m_sz, dt_s * 1e6 / double(m_Cycles));
			return false;
		}

		if (dt_s < 0.5)
			N <<= 1;

		return true;
	}
};

void RunBenchmark()
{
	Scalar::Native k1, k2;
	SetRandom(k1);
	SetRandom(k2);

/*	{
		BenchmarkMeter bm("scalar.Add");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Add(k2);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("scalar.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Mul(k2);

		} while (bm.ShouldContinue());
	}
*/

	{
		BenchmarkMeter bm("scalar.Inverse");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Inv();

		} while (bm.ShouldContinue());
	}

	Scalar k_;
/*
	{
		BenchmarkMeter bm("scalar.Export");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Export(k_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("scalar.Import");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Import(k_);

		} while (bm.ShouldContinue());
	}
*/

	{
		ScalarGenerator pwrGen;
		pwrGen.Initialize(7U);

		BenchmarkMeter bm("scalar.7-Pwr");
		SetRandom(k_.m_Value);
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				pwrGen.Calculate(k1, k_);

		} while (bm.ShouldContinue());
	}

	Point::Native p0, p1;

	Point p_;
	p_.m_Y = 0;

	SetRandom(p_.m_X);
	while (!p0.Import(p_))
		p_.m_X.Inc();

	SetRandom(p_.m_X);
	while (!p1.Import(p_))
		p_.m_X.Inc();

/*	{
		BenchmarkMeter bm("point.Negate");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = -p0;

		} while (bm.ShouldContinue());
	}
*/
	{
		BenchmarkMeter bm("point.Double");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p0 * Two;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("point.Add");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += p1;

		} while (bm.ShouldContinue());
	}

	{
		Mode::Scope scope(Mode::Fast);
		k1 = Zero;

		BenchmarkMeter bm("point.Multiply.Min");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		Mode::Scope scope(Mode::Fast);

		BenchmarkMeter bm("point.Multiply.Avg");
		do
		{
			SetRandom(k1);
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		Mode::Scope scope(Mode::Secure);

		BenchmarkMeter bm("point.Multiply.Sec");
		do
		{
			SetRandom(k1);
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		// result should be close to prev (i.e. constant-time)
		Mode::Scope scope(Mode::Secure);
		BenchmarkMeter bm("point.Multiply.Sec2");
		do
		{
			k1 = Zero;
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());

		p0 = p1;
	}

	{
		BenchmarkMeter bm("point.Export");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0.Export(p_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("point.Import");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0.Import(p_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("H.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Context::get().H * uint64_t(-1);

		} while (bm.ShouldContinue());
	}

	{
		k1 = uint64_t(-1);

		Point p2;
		p2.m_X = Zero;
		p2.m_Y = 0;

		while (!p0.Import(p2))
			p2.m_X.Inc();

		BenchmarkMeter bm("G.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Context::get().G * k1;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("Commit");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Commitment(k1, 275);

		} while (bm.ShouldContinue());
	}

	Hash::Value hv;

	{
		uint8_t pBuf[0x400];
		GenerateRandom(pBuf, sizeof(pBuf));

		BenchmarkMeter bm("Hash.Init.1K.Out");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				Hash::Processor()
					<< beam::Blob(pBuf, sizeof(pBuf))
					>> hv;
			}

		} while (bm.ShouldContinue());
	}

	Hash::Processor() << "abcd" >> hv;

	Signature sig;
	{
		BenchmarkMeter bm("signature.Sign");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.Sign(hv, k1);

		} while (bm.ShouldContinue());
	}

	p1 = Context::get().G * k1;
	{
		BenchmarkMeter bm("signature.Verify");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.IsValid(hv, p1);

		} while (bm.ShouldContinue());
	}

	Scalar::Native pA[InnerProduct::nDim];
	Scalar::Native pB[InnerProduct::nDim];

	for (size_t i = 0; i < _countof(pA); i++)
	{
		SetRandom(pA[i]);
		SetRandom(pB[i]);
	}

	InnerProduct sig2;

	Point::Native commAB;
	Scalar::Native dot;
	InnerProduct::get_Dot(dot, pA, pB);

	{
		BenchmarkMeter bm("InnerProduct.Sign");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig2.Create(commAB, dot, pA, pB);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("InnerProduct.Verify");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig2.IsValid(commAB, dot);

		} while (bm.ShouldContinue());
	}

	RangeProof::Confidential bp;
	RangeProof::CreatorParams cp;
	ZeroObject(cp.m_Kidv);
	SetRandom(cp.m_Seed.V);
	cp.m_Kidv.m_Value = 23110;

	{
		BenchmarkMeter bm("BulletProof.Sign");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				Oracle oracle;
				bp.Create(k1, cp, oracle);
			}

		} while (bm.ShouldContinue());
	}

	Point::Native comm = Commitment(k1, cp.m_Kidv.m_Value);

	{
		BenchmarkMeter bm("BulletProof.Verify");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				Oracle oracle;
				bp.IsValid(comm, oracle);
			}

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("BulletProof.Verify x100");

		const uint32_t nBatch = 100;
		bm.N = 10 * nBatch;

		typedef InnerProduct::BatchContextEx<100> MyBatch;
		std::unique_ptr<MyBatch> p(new MyBatch);
		p->m_bEnableBatch = true;

		InnerProduct::BatchContext::Scope scope(*p);

		do
		{
			for (uint32_t i = 0; i < bm.N; i += nBatch)
			{
				for (uint32_t n = 0; n < nBatch; n++)
				{
					Oracle oracle;
					bp.IsValid(comm, oracle);
				}

				verify_test(p->Flush());
			}

		} while (bm.ShouldContinue());
	}

	{
		AES::Encoder enc;
		enc.Init(hv.m_pData);
		AES::StreamCipher asc;
		asc.Reset();

		uint8_t pBuf[0x400];

		BenchmarkMeter bm("AES.XCrypt-1MB");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				for (size_t nSize = 0; nSize < 0x100000; nSize += sizeof(pBuf))
					asc.XCrypt(enc, pBuf, sizeof(pBuf));
			}

		} while (bm.ShouldContinue());
	}

	{
		uint8_t pBuf[0x400];

		BenchmarkMeter bm("Random-1K");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				GenRandom(pBuf, sizeof(pBuf));

		} while (bm.ShouldContinue());
	}


	{
		secp256k1_pedersen_commitment comm2;

		BenchmarkMeter bm("secp256k1.Commit");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				(void) secp256k1_pedersen_commit(g_psecp256k1, &comm2, k_.m_Value.m_pData, 78945, secp256k1_generator_h);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("secp256k1.G.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				secp256k1_ecmult_gen(g_psecp256k1, &p0.get_Raw(), &k1.get());

		} while (bm.ShouldContinue());
	}

}


} // namespace ECC

int main()
{
	g_psecp256k1 = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	beam::Rules::get().CA.Enabled = true;
	ECC::TestAll();
	ECC::RunBenchmark();

	secp256k1_context_destroy(g_psecp256k1);

    return g_TestsFailed ? -1 : 0;
}
