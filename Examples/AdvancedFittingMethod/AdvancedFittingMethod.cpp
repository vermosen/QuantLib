/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
Copyright (C) 2014 Jose Aparicio

This file is part of QuantLib, a free-software/open-source library
for financial quantitative analysts and developers - http://quantlib.org/

QuantLib is free software: you can redistribute it and/or modify it
under the terms of the QuantLib license.  You should have received a
copy of the license along with this program; if not, please email
<quantlib-dev@lists.sf.net>. The license is also available online at
<http://quantlib.org/license.shtml>.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/quantlib.hpp>

#include <boost/timer.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <boost/assign/std/vector.hpp>

#include <iostream>
#include <iomanip>

using namespace std;
using namespace QuantLib;
using namespace boost::assign;

#if defined(QL_ENABLE_SESSIONS)
namespace QuantLib {
	Integer sessionId() { return 0; }
}
#endif

int main(int, char*[]) {

	try {

		boost::timer timer;
		std::cout << std::endl;

		Calendar calendar = TARGET();
		Date todaysDate(19, March, 2014);
		// must be a business day
		todaysDate = calendar.adjust(todaysDate);

		Settings::instance().evaluationDate() = todaysDate;


		/* --------------------------------------------------------------
		SET UP BASKET PORTFOLIO
		-------------------------------------------------------------- */
		// build curves and issuers into a basket of ten names
		std::vector<Real> hazardRates;
		hazardRates +=
			//  0.01, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9;        
			0.001, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09;
		//  0.01,  0.01,  0.01,  0.01, 0.01,  0.01,  0.01,  0.01,  0.01,  0.01;        
		std::vector<std::string> names;
		for (Size i = 0; i<hazardRates.size(); i++)
			names.push_back(std::string("Acme") +
				boost::lexical_cast<std::string>(i));
		std::vector<Handle<DefaultProbabilityTermStructure> > defTS;
		for (Size i = 0; i<hazardRates.size(); i++) {
			defTS.push_back(Handle<DefaultProbabilityTermStructure>(
				boost::make_shared<FlatHazardRate>(0, TARGET(), hazardRates[i],
					Actual365Fixed())));
			defTS.back()->enableExtrapolation();
		}
		std::vector<Issuer> issuers;
		for (Size i = 0; i<hazardRates.size(); i++) {
			std::vector<QuantLib::Issuer::key_curve_pair> curves(1,
				std::make_pair(NorthAmericaCorpDefaultKey(
					EURCurrency(), QuantLib::SeniorSec,
					Period(), 1. // amount threshold
				), defTS[i]));
			issuers.push_back(Issuer(curves));
		}

		boost::shared_ptr<Pool> thePool = boost::make_shared<Pool>();
		for (Size i = 0; i<hazardRates.size(); i++)
			thePool->add(names[i], issuers[i], NorthAmericaCorpDefaultKey(
				EURCurrency(), QuantLib::SeniorSec, Period(), 1.));

		std::vector<DefaultProbKey> defaultKeys(hazardRates.size(),
			NorthAmericaCorpDefaultKey(EURCurrency(), SeniorSec, Period(), 1.));
		boost::shared_ptr<Basket> theBskt = boost::make_shared<Basket>(
			todaysDate,
			names, std::vector<Real>(hazardRates.size(), 100.), thePool,
			//   0.0, 0.78);
			0.03, .06);

		/* --------------------------------------------------------------
		SET UP DEFAULT LOSS MODELS
		-------------------------------------------------------------- */

		std::vector<Real> recoveries(hazardRates.size(), 0.4);

		Date calcDate(TARGET().advance(Settings::instance().evaluationDate(),
			Period(60, Months)));
		Real factorValue = 0.05;
		std::vector<std::vector<Real> > fctrsWeights(hazardRates.size(),
			std::vector<Real>(1, std::sqrt(factorValue)));

		// --- LHP model --------------------------
		boost::shared_ptr<DefaultLossModel> lmGLHP(
			boost::make_shared<GaussianLHPLossModel>(
				fctrsWeights[0][0] * fctrsWeights[0][0], recoveries));
		theBskt->setLossModel(lmGLHP);

		std::cout << "GLHP Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

		// --- G Binomial model --------------------
		boost::shared_ptr<GaussianConstantLossLM> ktLossLM(
			boost::make_shared<GaussianConstantLossLM>(fctrsWeights,
				recoveries, LatentModelIntegrationType::GaussianQuadrature,
				GaussianCopulaPolicy::initTraits()));
		boost::shared_ptr<DefaultLossModel> lmBinomial(
			boost::make_shared<GaussianBinomialLossModel>(ktLossLM));
		theBskt->setLossModel(lmBinomial);

		std::cout << "Gaussian Binomial Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

		// --- T Binomial model --------------------
		TCopulaPolicy::initTraits initT;
		initT.tOrders = std::vector<Integer>(2, 3);
		boost::shared_ptr<TConstantLossLM> ktTLossLM(
			boost::make_shared<TConstantLossLM>(fctrsWeights,
				recoveries,
				//LatentModelIntegrationType::GaussianQuadrature,
				LatentModelIntegrationType::Trapezoid,
				initT));
		boost::shared_ptr<DefaultLossModel> lmTBinomial(
			boost::make_shared<TBinomialLossModel>(ktTLossLM));
		theBskt->setLossModel(lmTBinomial);

		std::cout << "T Binomial Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

		// --- G Inhomogeneous model ---------------
		boost::shared_ptr<GaussianConstantLossLM> gLM(
			boost::make_shared<GaussianConstantLossLM>(fctrsWeights,
				recoveries,
				LatentModelIntegrationType::GaussianQuadrature,
				// g++ requires this when using make_shared
				GaussianCopulaPolicy::initTraits()));

		Size numBuckets = 100;
		boost::shared_ptr<DefaultLossModel> inhomogeneousLM(
			boost::make_shared<IHGaussPoolLossModel>(gLM, numBuckets));
		theBskt->setLossModel(inhomogeneousLM);

		std::cout << "G Inhomogeneous Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

		// --- G Random model ---------------------
		// Gaussian random joint default model:
		Size numSimulations = 100000;
		// Size numCoresUsed = 4;
		// Sobol, many cores
		boost::shared_ptr<DefaultLossModel> rdlmG(
			boost::make_shared<RandomDefaultLM<GaussianCopulaPolicy,
			RandomSequenceGenerator<
			BoxMullerGaussianRng<MersenneTwisterUniformRng> > > >(gLM,
				recoveries, numSimulations, 1.e-6, 2863311530));
		//boost::shared_ptr<DefaultLossModel> rdlmG(
		//    boost::make_shared<RandomDefaultLM<GaussianCopulaPolicy> >(gLM, 
		//        recoveries, numSimulations, 1.e-6, 2863311530));
		theBskt->setLossModel(rdlmG);

		std::cout << "Random G Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

		// --- StudentT Random model ---------------------
		// Sobol, many cores
		boost::shared_ptr<DefaultLossModel> rdlmT(
			boost::make_shared<RandomDefaultLM<TCopulaPolicy,
			RandomSequenceGenerator<
			PolarStudentTRng<MersenneTwisterUniformRng> > > >(ktTLossLM,
				recoveries, numSimulations, 1.e-6, 2863311530));
		//boost::shared_ptr<DefaultLossModel> rdlmT(
		//    boost::make_shared<RandomDefaultLM<TCopulaPolicy> >(ktTLossLM, 
		//        recoveries, numSimulations, 1.e-6, 2863311530));
		theBskt->setLossModel(rdlmT);

		std::cout << "Random T Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;


		// Spot Loss latent model: 
		std::vector<std::vector<Real> > fctrsWeightsRR(2 * hazardRates.size(),
			std::vector<Real>(1, std::sqrt(factorValue)));
		Real modelA = 2.2;
		boost::shared_ptr<GaussianSpotLossLM> sptLG(new GaussianSpotLossLM(
			fctrsWeightsRR, recoveries, modelA,
			LatentModelIntegrationType::GaussianQuadrature,
			GaussianCopulaPolicy::initTraits()));
		boost::shared_ptr<TSpotLossLM> sptLT(new TSpotLossLM(fctrsWeightsRR,
			recoveries, modelA,
			LatentModelIntegrationType::GaussianQuadrature, initT));


		// --- G Random Loss model ---------------------
		// Gaussian random joint default model:
		// Sobol, many cores
		boost::shared_ptr<DefaultLossModel> rdLlmG(
			boost::make_shared<RandomLossLM<GaussianCopulaPolicy> >(sptLG,
				numSimulations, 1.e-6, 2863311530));
		theBskt->setLossModel(rdLlmG);

		std::cout << "Random Loss G Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;

		// --- T Random Loss model ---------------------
		// Gaussian random joint default model:
		// Sobol, many cores
		boost::shared_ptr<DefaultLossModel> rdLlmT(
			boost::make_shared<RandomLossLM<TCopulaPolicy> >(sptLT,
				numSimulations, 1.e-6, 2863311530));
		theBskt->setLossModel(rdLlmT);

		std::cout << "Random Loss T Expected 10-Yr Losses: " << std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;





		// Base Correlation model set up to test cocherence with base LHP model
		std::vector<Period> bcTenors;
		bcTenors.push_back(Period(1, Years));
		bcTenors.push_back(Period(5, Years));
		std::vector<Real> bcLossPercentages;
		bcLossPercentages.push_back(0.03);
		bcLossPercentages.push_back(0.12);
		std::vector<std::vector<Handle<Quote> > > correls;
		// 
		std::vector<Handle<Quote> > corr1Y;
		// 3%
		corr1Y.push_back(Handle<Quote>(boost::shared_ptr<Quote>(
			new SimpleQuote(fctrsWeights[0][0] * fctrsWeights[0][0]))));
		// 12%
		corr1Y.push_back(Handle<Quote>(boost::shared_ptr<Quote>(
			new SimpleQuote(fctrsWeights[0][0] * fctrsWeights[0][0]))));
		correls.push_back(corr1Y);
		std::vector<Handle<Quote> > corr2Y;
		// 3%
		corr2Y.push_back(Handle<Quote>(boost::shared_ptr<Quote>(
			new SimpleQuote(fctrsWeights[0][0] * fctrsWeights[0][0]))));
		// 12%
		corr2Y.push_back(Handle<Quote>(boost::shared_ptr<Quote>(
			new SimpleQuote(fctrsWeights[0][0] * fctrsWeights[0][0]))));
		correls.push_back(corr2Y);
		boost::shared_ptr<BaseCorrelationTermStructure<BilinearInterpolation> >
			correlSurface(
				new BaseCorrelationTermStructure<BilinearInterpolation>(
					// first one would do, all should be the same.
					defTS[0]->settlementDays(),
					defTS[0]->calendar(),
					Unadjusted,
					bcTenors,
					bcLossPercentages,
					correls,
					Actual365Fixed()
					)
			);
		Handle<BaseCorrelationTermStructure<BilinearInterpolation> >
			correlHandle(correlSurface);
		boost::shared_ptr<DefaultLossModel> bcLMG_LHP_Bilin(
			boost::make_shared<GaussianLHPFlatBCLM>(correlHandle, recoveries,
				GaussianCopulaPolicy::initTraits()));

		theBskt->setLossModel(bcLMG_LHP_Bilin);

		std::cout << "Base Correlation GLHP Expected 10-Yr Losses: "
			<< std::endl;
		std::cout << theBskt->expectedTrancheLoss(calcDate) << std::endl;


		Real seconds = timer.elapsed();
		Integer hours = Integer(seconds / 3600);
		seconds -= hours * 3600;
		Integer minutes = Integer(seconds / 60);
		seconds -= minutes * 60;
		cout << "Run completed in ";
		if (hours > 0)
			cout << hours << " h ";
		if (hours > 0 || minutes > 0)
			cout << minutes << " m ";
		cout << fixed << setprecision(0)
			<< seconds << " s" << endl;

		return 0;
	}
	catch (exception& e) {
		cerr << e.what() << endl;
		return 1;
	}
	catch (...) {
		cerr << "unknown error" << endl;
		return 1;
	}
}

