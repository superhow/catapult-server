// /**
// *** Copyright (c) 2016-present,
// *** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
// ***
// *** This file is part of Catapult.
// ***
// *** Catapult is free software: you can redistribute it and/or modify
// *** it under the terms of the GNU Lesser General Public License as published by
// *** the Free Software Foundation, either version 3 of the License, or
// *** (at your option) any later version.
// ***
// *** Catapult is distributed in the hope that it will be useful,
// *** but WITHOUT ANY WARRANTY; without even the implied warranty of
// *** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *** GNU Lesser General Public License for more details.
// ***
// *** You should have received a copy of the GNU Lesser General Public License
// *** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
// **/

// #include "src/validators/Validators.h"
// #include "src/plugins/MultisigAccountModificationTransactionPlugin.h"
// #include "catapult/model/TransactionPlugin.h"
// #include "tests/test/MultisigCacheTestUtils.h"
// #include "tests/test/MultisigTestUtils.h"
// #include "tests/test/core/mocks/MockTransaction.h"
// #include "tests/test/plugins/ValidatorTestUtils.h"
// #include "tests/TestHarness.h"

// namespace catapult { namespace validators {

// #define TEST_CLASS MultisigAggregateEligibleCosignatoriesValidatorTests

// 	DEFINE_COMMON_VALIDATOR_TESTS(MultisigAggregateEligibleCosignatories, model::TransactionRegistry())

// 	namespace {
// 		constexpr auto Failure_Result = Failure_Aggregate_Ineligible_Cosignatories;

// 		auto CreateTransactionRegistry() {
// 			// use a registry with mock and multilevel multisig transactions registered
// 			//   mock is used to test default behavior
// 			//   multilevel multisig is used to test transactions with custom approval requirements
// 			model::TransactionRegistry transactionRegistry;
// 			transactionRegistry.registerPlugin(mocks::CreateMockTransactionPlugin(mocks::PluginOptionFlags::Not_Top_Level));
// 			transactionRegistry.registerPlugin(plugins::CreateMultisigAccountModificationTransactionPlugin());
// 			return transactionRegistry;
// 		}

// 		void AssertValidationResult(
// 				ValidationResult expectedResult,
// 				const cache::CatapultCache& cache,
// 				const Key& signer,
// 				const std::vector<Key>& embeddedSigners,
// 				const std::vector<Key>& cosignatories) {
// 			// Arrange: setup transactions
// 			std::vector<uint8_t> txBuffer(sizeof(model::EmbeddedTransaction) * embeddedSigners.size());
// 			auto* pTransactions = reinterpret_cast<model::EmbeddedTransaction*>(txBuffer.data());
// 			for (auto i = 0u; i < embeddedSigners.size(); ++i) {
// 				auto& transaction = pTransactions[i];
// 				transaction.Type = mocks::MockTransaction::Entity_Type;
// 				transaction.Size = sizeof(model::EmbeddedTransaction);
// 				transaction.SignerPublicKey = embeddedSigners[i];
// 			}

// 			// - setup cosignatures and registry
// 			auto cosignatures = test::GenerateCosignaturesFromCosignatories(cosignatories);
// 			auto transactionRegistry = CreateTransactionRegistry();

// 			using Notification = model::AggregateCosignaturesNotification;
// 			Notification notification(signer, embeddedSigners.size(), pTransactions, cosignatures.size(), cosignatures.data());
// 			auto pValidator = CreateMultisigAggregateEligibleCosignatoriesValidator(transactionRegistry);

// 			// Act:
// 			auto result = test::ValidateNotification(*pValidator, notification, cache);

// 			// Assert:
// 			EXPECT_EQ(expectedResult, result);
// 		}
// 	}

// 	// region unknown account / known non-multisig account

// 	namespace {
// 		struct UnknownAccountTraits {
// 			static auto CreateCache(const Key&) {
// 				// return an empty cache
// 				return test::MultisigCacheFactory::Create();
// 			}
// 		};

// 		struct CosignatoryAccountTraits {
// 			static auto CreateCache(const Key& aggregateSigner) {
// 				auto cache = test::MultisigCacheFactory::Create();
// 				auto cacheDelta = cache.createDelta();

// 				// make the aggregate signer a cosignatory of a different account
// 				test::MakeMultisig(cacheDelta, test::GenerateRandomByteArray<Key>(), { aggregateSigner });

// 				cache.commit(Height());
// 				return cache;
// 			}
// 		};

// 		template<typename TTraits>
// 		static void AssertValidationResult(
// 				ValidationResult expectedResult,
// 				const Key& aggregateSigner,
// 				const std::vector<Key>& embeddedSigners,
// 				const std::vector<Key>& cosignatories) {
// 			// Arrange:
// 			auto cache = TTraits::CreateCache(aggregateSigner);

// 			// Assert:
// 			AssertValidationResult(expectedResult, cache, aggregateSigner, embeddedSigners, cosignatories);
// 		}
// 	}

// #define NON_MULTISIG_TRAITS_TEST(TEST_NAME) \
// 	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)(); \
// 	TEST(TEST_CLASS, TEST_NAME##_UnknownAccount) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<UnknownAccountTraits>(); } \
// 	TEST(TEST_CLASS, TEST_NAME##_CosignatoryAccount) { TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)<CosignatoryAccountTraits>(); } \
// 	template<typename TTraits> void TRAITS_TEST_NAME(TEST_CLASS, TEST_NAME)()

// 	NON_MULTISIG_TRAITS_TEST(CosignatoryIsEligibleWhenItMatchesEmbeddedTransactionSigner) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);

// 		// Assert: valid because both aggregate signer and cosignatory are also embedded transaction signers
// 		auto i = 0u;
// 		for (const auto& embeddedSigner : embeddedSigners) {
// 			CATAPULT_LOG(debug) << "cosigning with cosignatory " << i++;
// 			AssertValidationResult<TTraits>(ValidationResult::Success, embeddedSigners[0], embeddedSigners, { embeddedSigner });
// 		}
// 	}

// 	NON_MULTISIG_TRAITS_TEST(CosignatoryIsIneligibleWhenItIsUnrelatedAccount) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto ineligibleSigner = test::GenerateRandomByteArray<Key>();

// 		// Assert: invalid because cosignatory is not an embedded transaction signer
// 		AssertValidationResult<TTraits>(Failure_Result, embeddedSigners[0], embeddedSigners, { ineligibleSigner });
// 	}

// 	NON_MULTISIG_TRAITS_TEST(AggregateSignerIsIneligibleWhenItIsUnrelatedAccount) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto ineligibleSigner = test::GenerateRandomByteArray<Key>();

// 		// Assert: invalid because aggregate signer is not an embedded transaction signer
// 		AssertValidationResult<TTraits>(Failure_Result, ineligibleSigner, embeddedSigners, { embeddedSigners[1] });
// 	}

// 	NON_MULTISIG_TRAITS_TEST(CosignatoriesAreEligibleWhenAllMatchEmbeddedTransactionSigners) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);

// 		// Assert: valid because aggregate signer and all cosignatories are also embedded transaction signers
// 		AssertValidationResult<TTraits>(ValidationResult::Success, embeddedSigners[0], embeddedSigners, embeddedSigners);
// 	}

// 	NON_MULTISIG_TRAITS_TEST(CosignatoriesAreIneligibleWhenAnyDoesNotMatchEmbeddedTransactionSigner) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto cosignatories = { embeddedSigners[0], test::GenerateRandomByteArray<Key>(), embeddedSigners[2] };

// 		// Assert: invalid because a single cosignatory is not an embedded transaction signer
// 		AssertValidationResult<TTraits>(Failure_Result, embeddedSigners[0], embeddedSigners, cosignatories);
// 	}

// 	// endregion

// 	// region multisig

// 	namespace {
// 		auto CreateCacheWithSingleLevelMultisig(const Key& embeddedSigner, const std::vector<Key>& cosignatories) {
// 			auto cache = test::MultisigCacheFactory::Create();
// 			auto cacheDelta = cache.createDelta();

// 			test::MakeMultisig(cacheDelta, embeddedSigner, cosignatories, 3, 4); // make a 3-4-X multisig

// 			cache.commit(Height());
// 			return cache;
// 		}
// 	}

// 	TEST(TEST_CLASS, CosignatoryIsEligibleWhenItMatchesDirectEmbeddedTransactionSignerCosignatory) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto embeddedCosignatories = test::GenerateRandomDataVector<Key>(4);
// 		auto cache = CreateCacheWithSingleLevelMultisig(embeddedSigners[1], embeddedCosignatories);

// 		// Assert: valid because embeddedCosignatory is a direct cosignatory of an embedded tx signer
// 		auto i = 0u;
// 		for (const auto& embeddedCosignatory : embeddedCosignatories) {
// 			CATAPULT_LOG(debug) << "cosigning with cosignatory " << i++;
// 			AssertValidationResult(ValidationResult::Success, cache, embeddedSigners[0], embeddedSigners, { embeddedCosignatory });
// 		}
// 	}

// 	TEST(TEST_CLASS, AggregateSignerIsEligibleWhenItMatchesDirectEmbeddedTransactionSignerCosignatory) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto embeddedCosignatories = test::GenerateRandomDataVector<Key>(4);
// 		auto cache = CreateCacheWithSingleLevelMultisig(embeddedSigners[1], embeddedCosignatories);

// 		// Assert: valid because embeddedCosignatory is a direct cosignatory of an embedded tx signer
// 		auto i = 0u;
// 		for (const auto& embeddedCosignatory : embeddedCosignatories) {
// 			CATAPULT_LOG(debug) << "signing aggregate with " << i++;
// 			AssertValidationResult(ValidationResult::Success, cache, embeddedCosignatory, embeddedSigners, { embeddedSigners[0] });
// 		}
// 	}

// 	namespace {
// 		auto CreateCacheWithMultilevelMultisig(
// 				const Key& embeddedSigner,
// 				const std::vector<Key>& cosignatories,
// 				const std::vector<Key>& secondLevelCosignatories) {
// 			auto cache = test::MultisigCacheFactory::Create();
// 			auto cacheDelta = cache.createDelta();

// 			test::MakeMultisig(cacheDelta, embeddedSigner, cosignatories, 2, 3); // make a 2-3-X multisig
// 			test::MakeMultisig(cacheDelta, cosignatories[1], secondLevelCosignatories, 3, 4); // make a 3-4-X multisig

// 			cache.commit(Height());
// 			return cache;
// 		}
// 	}

// 	TEST(TEST_CLASS, CosignatoryIsEligibleWhenItMatchesIndirectEmbeddedTransactionSignerCosignatory) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto embeddedCosignatories = test::GenerateRandomDataVector<Key>(4);
// 		auto embeddedCosignatoriesL2 = test::GenerateRandomDataVector<Key>(5);
// 		auto cache = CreateCacheWithMultilevelMultisig(embeddedSigners[1], embeddedCosignatories, embeddedCosignatoriesL2);

// 		// Assert: valid because embeddedCosignatory is a second-level cosignatory of an embedded tx signer
// 		auto i = 0u;
// 		for (const auto& embeddedCosignatory : embeddedCosignatoriesL2) {
// 			CATAPULT_LOG(debug) << "cosigning with cosignatory " << i++;
// 			AssertValidationResult(ValidationResult::Success, cache, embeddedSigners[0], embeddedSigners, { embeddedCosignatory });
// 		}
// 	}

// 	TEST(TEST_CLASS, AggregateSignerIsEligibleWhenItMatchesIndirectEmbeddedTransactionSignerCosignatory) {
// 		// Arrange:
// 		auto embeddedSigners = test::GenerateRandomDataVector<Key>(3);
// 		auto embeddedCosignatories = test::GenerateRandomDataVector<Key>(4);
// 		auto embeddedCosignatoriesL2 = test::GenerateRandomDataVector<Key>(5);
// 		auto cache = CreateCacheWithMultilevelMultisig(embeddedSigners[1], embeddedCosignatories, embeddedCosignatoriesL2);

// 		// Assert: valid because embeddedCosignatory is a second-level cosignatory of an embedded tx signer
// 		auto i = 0u;
// 		for (const auto& embeddedCosignatory : embeddedCosignatoriesL2) {
// 			CATAPULT_LOG(debug) << "signing aggregate with " << i++;
// 			AssertValidationResult(ValidationResult::Success, cache, embeddedCosignatory, embeddedSigners, { embeddedSigners[0] });
// 		}
// 	}

// 	// endregion

// 	// region multisig account modification handling

// 	namespace {
// 		void AssertValidationResult(
// 				ValidationResult expectedResult,
// 				const cache::CatapultCache& cache,
// 				const Key& signer,
// 				const model::EmbeddedTransaction& transaction,
// 				const std::vector<Key>& cosignatories) {
// 			// Arrange: setup cosignatures and registry
// 			auto cosignatures = test::GenerateCosignaturesFromCosignatories(cosignatories);
// 			auto transactionRegistry = CreateTransactionRegistry();

// 			using Notification = model::AggregateCosignaturesNotification;
// 			Notification notification(signer, 1, &transaction, cosignatures.size(), cosignatures.data());
// 			auto pValidator = CreateMultisigAggregateEligibleCosignatoriesValidator(transactionRegistry);

// 			// Act:
// 			auto result = test::ValidateNotification(*pValidator, notification, cache);

// 			// Assert:
// 			EXPECT_EQ(expectedResult, result);
// 		}
// 	}

// 	TEST(TEST_CLASS, MultisigAccountModificationAddedAccountsGainEligibility) {
// 		// Arrange:
// 		auto signer = test::GenerateRandomByteArray<Key>();
// 		auto pTransaction = test::CreateMultisigAccountModificationTransaction(signer, 2, 2);
// 		auto cache = test::MultisigCacheFactory::Create();

// 		// Assert: added accounts are eligible cosignatories even though they aren't in the multisig cache
// 		auto i = 0u;
// 		const auto* pAddressAdditions = pTransaction->AddressAdditionsPtr();
// 		for (const auto& cosignatory : { signer, pAddressAdditions[0], pAddressAdditions[1] }) {
// 			CATAPULT_LOG(debug) << "cosigning with cosignatory " << i++;
// 			AssertValidationResult(ValidationResult::Success, cache, signer, *pTransaction, { cosignatory });
// 		}
// 	}

// 	TEST(TEST_CLASS, MultisigAccountModificationDeletedAccountsDoNotGainEligibility) {
// 		// Arrange:
// 		auto signer = test::GenerateRandomByteArray<Key>();
// 		auto pTransaction = test::CreateMultisigAccountModificationTransaction(signer, 2, 2);
// 		auto cache = test::MultisigCacheFactory::Create();

// 		// Assert: deleted accounts do not have any special eligibility privileges
// 		auto i = 0u;
// 		const auto* pAddressDeletions = pTransaction->AddressDeletionsPtr();
// 		for (const auto& cosignatory : { pAddressDeletions[0], pAddressDeletions[1] }) {
// 			CATAPULT_LOG(debug) << "cosigning with cosignatory " << i++;
// 			AssertValidationResult(Failure_Result, cache, signer, *pTransaction, { cosignatory });
// 		}
// 	}

// 	TEST(TEST_CLASS, MultisigAccountDeletedAccountsRetainExistingEligibility) {
// 		// Arrange: delete the (original eligible) embedded tx signer
// 		auto signer = test::GenerateRandomByteArray<Key>();
// 		auto pTransaction = test::CreateMultisigAccountModificationTransaction(signer, 2, 2);
// 		pTransaction->AddressDeletionsPtr()[0] = signer;
// 		auto cache = test::MultisigCacheFactory::Create();

// 		// Assert: existing eligibility is retained
// 		//         (the embedded tx signer account is still allowed to sign even though it was deleted from a multisig account)
// 		AssertValidationResult(ValidationResult::Success, cache, signer, *pTransaction, { signer });
// 	}

// 	// endregion
// }}
