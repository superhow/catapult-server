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

#include "src/observers/Observers.h"
#include "tests/test/core/NotificationTestUtils.h"
#include "tests/test/plugins/AccountObserverTestContext.h"
#include "tests/test/plugins/ObserverTestUtils.h"
#include "tests/test/core/AddressTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace observers {

#define TEST_CLASS HighValueAccountObserverTests

	DEFINE_COMMON_OBSERVER_TESTS(HighValueAccount, NotifyMode::Commit)

	namespace {
		constexpr auto Harvesting_Mosaic_Id = MosaicId(9876);
		constexpr auto Min_Harvester_Balance = Amount(1'000'000);

		class TestContext : public test::AccountObserverTestContext {
		public:
			explicit TestContext(NotifyMode notifyMode)
					: test::AccountObserverTestContext(notifyMode, Height(123), CreateBlockChainConfiguration())
			{}

		public:
			auto highValueAddresses() {
				return cache().sub<cache::AccountStateCache>().highValueAddresses().Current;
			}

		public:
			void addAccount(const Address& address, Amount balance) {
				auto& accountStateCache = cache().sub<cache::AccountStateCache>();
				accountStateCache.addAccount(address, Height(123));

				auto accountStateIter = accountStateCache.find(address);
				accountStateIter.get().Balances.credit(Harvesting_Mosaic_Id, balance);
			}

		private:
			static model::BlockChainConfiguration CreateBlockChainConfiguration() {
				auto config = model::BlockChainConfiguration::Uninitialized();
				config.HarvestingMosaicId = Harvesting_Mosaic_Id;
				config.MinHarvesterBalance = Min_Harvester_Balance;
				return config;
			}
		};
	}

	TEST(TEST_CLASS, HighValueAccountsAreUpdatedWhenModeMatches) {
		// Arrange:
		auto addresses = test::GenerateRandomAddresses(3);
		TestContext context(NotifyMode::Commit);

		context.addAccount(addresses[0], Min_Harvester_Balance);
		context.addAccount(addresses[1], Min_Harvester_Balance - Amount(1));
		context.addAccount(addresses[2], Min_Harvester_Balance + Amount(1));

		auto pObserver = CreateHighValueAccountObserver(NotifyMode::Commit);

		// Act:
		test::ObserveNotification(*pObserver, test::CreateBlockNotification(), context);

		// Assert: modes match, so high value accounts should be updated
		EXPECT_EQ(model::AddressSet({ addresses[0], addresses[2] }), context.highValueAddresses());
	}

	TEST(TEST_CLASS, HighValueAccountsAreNotUpdatedWhenModeDoesNotMatch) {
		// Arrange:
		auto addresses = test::GenerateRandomAddresses(3);
		TestContext context(NotifyMode::Commit);

		context.addAccount(addresses[0], Min_Harvester_Balance);
		context.addAccount(addresses[1], Min_Harvester_Balance - Amount(1));
		context.addAccount(addresses[2], Min_Harvester_Balance + Amount(1));

		auto pObserver = CreateHighValueAccountObserver(NotifyMode::Rollback);

		// Act:
		test::ObserveNotification(*pObserver, test::CreateBlockNotification(), context);

		// Assert: modes don't match, so high value accounts should be unchanged
		EXPECT_EQ(model::AddressSet(), context.highValueAddresses());
	}
}}
