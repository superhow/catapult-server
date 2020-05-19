/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "catapult/cache_core/AccountStateCacheUtils.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/model/Address.h"
#include "tests/test/cache/AccountStateCacheTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace cache {

#define TEST_CLASS AccountStateCacheUtilsTests

	namespace {
		class TestContext {
		public:
			TestContext()
					: m_cache(CacheConfiguration(), test::CreateDefaultAccountStateCacheOptions())
					, m_pDelta(m_cache.createDelta())
			{}

		public:
			auto& cache() {
				return *m_pDelta;
			}

			template<typename TKey>
			auto addAccount(const TKey& key) {
				auto& cacheDelta = cache();
				cacheDelta.addAccount(key, Height(123));
				return cacheDelta.find(key);
			}

			void addAccount(const Key& mainPublicKey, state::AccountType accountType, const Key& remotePublicKey) {
				auto accountStateIter = addAccount(mainPublicKey);
				accountStateIter.get().AccountType = accountType;
				accountStateIter.get().SupplementalAccountKeys.linkedPublicKey().set(remotePublicKey);
			}

		private:
			cache::AccountStateCache m_cache;
			cache::LockedCacheDelta<cache::AccountStateCacheDelta> m_pDelta;
		};

		Address ToAddress(const Key& key) {
			return model::PublicKeyToAddress(key, test::CreateDefaultAccountStateCacheOptions().NetworkIdentifier);
		}
	}

	// region successful forward

	TEST(TEST_CLASS, CanForwardUnlinkedAccount) {
		// Arrange:
		TestContext context;
		auto address = test::GenerateRandomByteArray<Address>();
		context.addAccount(address);

		// Act:
		ProcessForwardedAccountState(context.cache(), address, [&address](const auto& accountState) {
			// Assert:
			EXPECT_EQ(address, accountState.Address);
		});
	}

	TEST(TEST_CLASS, CanForwardMainAccount) {
		// Arrange:
		TestContext context;
		auto mainPublicKey = test::GenerateRandomByteArray<Key>();
		context.addAccount(mainPublicKey, state::AccountType::Main, test::GenerateRandomByteArray<Key>());

		// Act:
		ProcessForwardedAccountState(context.cache(), ToAddress(mainPublicKey), [&mainPublicKey](const auto& accountState) {
			// Assert:
			EXPECT_EQ(mainPublicKey, accountState.PublicKey);
		});
	}

	TEST(TEST_CLASS, CanForwardRemoteAccount) {
		// Arrange:
		TestContext context;
		auto mainPublicKey = test::GenerateRandomByteArray<Key>();
		auto remotePublicKey = test::GenerateRandomByteArray<Key>();
		context.addAccount(mainPublicKey, state::AccountType::Main, remotePublicKey);
		context.addAccount(remotePublicKey, state::AccountType::Remote, mainPublicKey);

		// Act:
		ProcessForwardedAccountState(context.cache(), ToAddress(remotePublicKey), [&mainPublicKey](const auto& accountState) {
			// Assert: main account is returned
			EXPECT_EQ(mainPublicKey, accountState.PublicKey);
		});
	}

	// endregion

	// region forward failure

	TEST(TEST_CLASS, CannotForwardWhenMainAccountIsNotPresent) {
		// Arrange:
		TestContext context;
		auto mainPublicKey = test::GenerateRandomByteArray<Key>();
		auto remotePublicKey = test::GenerateRandomByteArray<Key>();
		context.addAccount(remotePublicKey, state::AccountType::Remote, mainPublicKey);

		// Act + Assert:
		EXPECT_THROW(
				ProcessForwardedAccountState(context.cache(), ToAddress(remotePublicKey), [](const auto&) {}),
				catapult_invalid_argument);
	}

	TEST(TEST_CLASS, CannotForwardWhenMainHasInvalidAccountType) {
		// Arrange:
		TestContext context;
		auto mainPublicKey = test::GenerateRandomByteArray<Key>();
		auto remotePublicKey = test::GenerateRandomByteArray<Key>();
		context.addAccount(mainPublicKey, state::AccountType::Remote, remotePublicKey);
		context.addAccount(remotePublicKey, state::AccountType::Remote, mainPublicKey);

		// Act + Assert:
		EXPECT_THROW(
				ProcessForwardedAccountState(context.cache(), ToAddress(remotePublicKey), [](const auto&) {}),
				catapult_runtime_error);
	}

	TEST(TEST_CLASS, CannotForwardWhenMainHasInvalidKey) {
		// Arrange: main account does not link-back to remote key
		TestContext context;
		auto mainPublicKey = test::GenerateRandomByteArray<Key>();
		auto remotePublicKey = test::GenerateRandomByteArray<Key>();
		context.addAccount(mainPublicKey, state::AccountType::Main, test::GenerateRandomByteArray<Key>());
		context.addAccount(remotePublicKey, state::AccountType::Remote, mainPublicKey);

		// Act + Assert:
		EXPECT_THROW(
				ProcessForwardedAccountState(context.cache(), ToAddress(remotePublicKey),
				[](const auto&) {}), catapult_runtime_error);
	}

	// endregion
}}
