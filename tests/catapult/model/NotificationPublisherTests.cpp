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

#include "catapult/model/NotificationPublisher.h"
#include "catapult/model/Address.h"
#include "tests/test/core/BlockTestUtils.h"
#include "tests/test/core/mocks/MockNotificationSubscriber.h"
#include "tests/test/core/mocks/MockTransaction.h"
#include "tests/test/nodeps/NumericTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace model {

#define TEST_CLASS NotificationPublisherTests

	namespace {
		constexpr auto Currency_Mosaic_Id = UnresolvedMosaicId(1234);

		constexpr auto Plugin_Option_Flags = static_cast<mocks::PluginOptionFlags>(
				utils::to_underlying_type(mocks::PluginOptionFlags::Custom_Buffers)
				| utils::to_underlying_type(mocks::PluginOptionFlags::Publish_Custom_Notifications));

		template<typename TEntity, typename TAssertSubFunc>
		void PublishAll(const TEntity& entity, PublicationMode mode, TAssertSubFunc assertSub) {
			// Arrange:
			mocks::MockNotificationSubscriber sub;

			auto registry = mocks::CreateDefaultTransactionRegistry(Plugin_Option_Flags);
			auto pPub = CreateNotificationPublisher(registry, Currency_Mosaic_Id, mode);

			// Act:
			auto hash = test::GenerateRandomByteArray<Hash256>();
			pPub->publish(WeakEntityInfo(entity, hash), sub);

			// Assert:
			assertSub(sub);
		}

		template<typename TEntity, typename TAssertSubFunc>
		void PublishAll(const TEntity& entity, TAssertSubFunc assertSub) {
			PublishAll(entity, PublicationMode::All, assertSub);
		}

		template<typename TNotification, typename TAssertNotification>
		void PublishOne(const WeakEntityInfo& entityInfo, TAssertNotification assertNotification) {
			// Arrange:
			mocks::MockTypedNotificationSubscriber<TNotification> sub;

			auto registry = mocks::CreateDefaultTransactionRegistry(Plugin_Option_Flags);
			auto pPub = CreateNotificationPublisher(registry, Currency_Mosaic_Id);

			// Act:
			pPub->publish(entityInfo, sub);

			// Assert:
			ASSERT_EQ(1u, sub.numMatchingNotifications());
			assertNotification(sub.matchingNotifications()[0]);
		}

		template<typename TNotification, typename TEntity, typename TAssertNotification>
		void PublishOne(const TEntity& entity, const Hash256& hash, TAssertNotification assertNotification) {
			// Act:
			PublishOne<TNotification>(WeakEntityInfo(entity, hash), assertNotification);
		}

		template<typename TNotification, typename TEntity, typename TAssertNotification>
		void PublishOne(const TEntity& entity, TAssertNotification assertNotification) {
			// Act:
			PublishOne<TNotification>(entity, test::GenerateRandomByteArray<Hash256>(), assertNotification);
		}
	}

	// region block

	TEST(TEST_CLASS, CanRaiseBlockSourceChangeNotifications) {
		// Arrange:
		auto pBlock = test::GenerateEmptyRandomBlock();

		// Act:
		PublishOne<SourceChangeNotification>(*pBlock, [](const auto& notification) {
			// Assert:
			EXPECT_EQ(0u, notification.PrimaryId);
			EXPECT_EQ(0u, notification.SecondaryId);
			EXPECT_EQ(SourceChangeNotification::SourceChangeType::Absolute, notification.PrimaryChangeType);
			EXPECT_EQ(SourceChangeNotification::SourceChangeType::Absolute, notification.SecondaryChangeType);
		});
	}

	TEST(TEST_CLASS, CanRaiseBlockAccountNotifications_WithBeneficiary) {
		// Arrange:
		auto pBlock = test::GenerateEmptyRandomBlock();
		test::FillWithRandomData(pBlock->SignerPublicKey);
		test::FillWithRandomData(pBlock->BeneficiaryPublicKey);

		// Act:
		PublishAll(*pBlock, [&block = *pBlock](const auto& sub) {
			// Assert:
			EXPECT_EQ(6u, sub.numNotifications());
			EXPECT_EQ(0u, sub.numAddresses());
			EXPECT_EQ(2u, sub.numKeys());

			EXPECT_TRUE(sub.contains(block.SignerPublicKey));
			EXPECT_TRUE(sub.contains(block.BeneficiaryPublicKey));
		});
	}

	TEST(TEST_CLASS, CanRaiseBlockAccountNotifications_WithoutBeneficiary) {
		// Arrange:
		auto pBlock = test::GenerateEmptyRandomBlock();
		test::FillWithRandomData(pBlock->SignerPublicKey);
		pBlock->BeneficiaryPublicKey = pBlock->SignerPublicKey;

		// Act:
		PublishAll(*pBlock, [&block = *pBlock](const auto& sub) {
			// Assert:
			EXPECT_EQ(5u, sub.numNotifications());
			EXPECT_EQ(0u, sub.numAddresses());
			EXPECT_EQ(1u, sub.numKeys());

			EXPECT_TRUE(sub.contains(block.SignerPublicKey));
		});
	}

	TEST(TEST_CLASS, CanRaiseBlockEntityNotifications) {
		// Arrange:
		auto pBlock = test::GenerateEmptyRandomBlock();
		pBlock->Version = 0x5A;
		pBlock->Network = static_cast<NetworkIdentifier>(0x11);

		// Act:
		PublishOne<EntityNotification>(*pBlock, [](const auto& notification) {
			// Assert:
			auto expectedVersion = Block::Current_Version;
			EXPECT_EQ(static_cast<NetworkIdentifier>(0x11), notification.NetworkIdentifier);
			EXPECT_EQ(0x5Au, notification.EntityVersion);
			EXPECT_EQ(expectedVersion, notification.MinVersion);
			EXPECT_EQ(expectedVersion, notification.MaxVersion);
		});
	}

	TEST(TEST_CLASS, CanRaiseBlockSignatureNotifications) {
		// Arrange:
		auto pBlock = test::GenerateEmptyRandomBlock();
		test::FillWithRandomData(pBlock->SignerPublicKey);
		test::FillWithRandomData(pBlock->Signature);

		// Act:
		PublishOne<SignatureNotification>(*pBlock, [&block = *pBlock](const auto& notification) {
			// Assert:
			EXPECT_EQ(block.SignerPublicKey, notification.SignerPublicKey);
			EXPECT_EQ(block.Signature, notification.Signature);
			EXPECT_EQ(test::AsVoidPointer(&block.Version), test::AsVoidPointer(notification.Data.pData));
			EXPECT_EQ(sizeof(BlockHeader) - VerifiableEntity::Header_Size - Block::Footer_Size, notification.Data.Size);
			EXPECT_EQ(SignatureNotification::ReplayProtectionMode::Disabled, notification.DataReplayProtectionMode);
		});
	}

	namespace {
		std::unique_ptr<Block> GenerateBlockWithTransactionSizes(const std::vector<Amount>& fees) {
			test::ConstTransactions transactions;
			for (auto fee : fees) {
				auto pTransaction = test::GenerateRandomTransactionWithSize(fee.unwrap());
				pTransaction->MaxFee = Amount(10 * fee.unwrap());
				transactions.push_back(std::move(pTransaction));
			}

			auto pBlock = test::GenerateBlockWithTransactions(transactions);
			test::FillWithRandomData(pBlock->SignerPublicKey);
			return pBlock;
		}
	}

	TEST(TEST_CLASS, CanRaiseBlockNotifications_BlockWithoutTransactions) {
		// Arrange:
		auto pBlock = GenerateBlockWithTransactionSizes({});
		pBlock->Timestamp = Timestamp(123);
		pBlock->Difficulty = Difficulty(575);
		pBlock->FeeMultiplier = BlockFeeMultiplier(3);

		// Act:
		PublishOne<BlockNotification>(*pBlock, [&block = *pBlock](const auto& notification) {
			// Assert:
			EXPECT_EQ(block.SignerPublicKey, notification.Harvester);
			EXPECT_EQ(block.BeneficiaryPublicKey, notification.Beneficiary);
			EXPECT_EQ(Timestamp(123), notification.Timestamp);
			EXPECT_EQ(Difficulty(575), notification.Difficulty);
			EXPECT_EQ(BlockFeeMultiplier(3), notification.FeeMultiplier);
			EXPECT_EQ(Amount(0), notification.TotalFee);
			EXPECT_EQ(0u, notification.NumTransactions);
		});
	}

	TEST(TEST_CLASS, CanRaiseBlockNotifications_BlockWithTransactions) {
		// Arrange:
		auto pBlock = GenerateBlockWithTransactionSizes({ Amount(211), Amount(225), Amount(217) });
		pBlock->Timestamp = Timestamp(432);
		pBlock->Difficulty = Difficulty(575);
		pBlock->FeeMultiplier = BlockFeeMultiplier(3);

		// Act:
		PublishOne<BlockNotification>(*pBlock, [&block = *pBlock](const auto& notification) {
			// Assert:
			EXPECT_EQ(block.SignerPublicKey, notification.Harvester);
			EXPECT_EQ(block.BeneficiaryPublicKey, notification.Beneficiary);
			EXPECT_EQ(Timestamp(432), notification.Timestamp);
			EXPECT_EQ(Difficulty(575), notification.Difficulty);
			EXPECT_EQ(BlockFeeMultiplier(3), notification.FeeMultiplier);
			EXPECT_EQ(Amount(3 * 653), notification.TotalFee);
			EXPECT_EQ(3u, notification.NumTransactions);
		});
	}

	TEST(TEST_CLASS, CanPublishBlockNotificationsWithModeBasic) {
		// Arrange:
		auto pBlock = GenerateBlockWithTransactionSizes({});

		// Act:
		PublishAll(*pBlock, PublicationMode::Basic, [](const auto& sub) {
			// Assert: no notifications were suppressed (blocks do not have custom notifications)
			ASSERT_EQ(6u, sub.numNotifications());
			EXPECT_EQ(Core_Source_Change_Notification, sub.notificationTypes()[0]);
			EXPECT_EQ(Core_Register_Account_Public_Key_Notification, sub.notificationTypes()[1]);
			EXPECT_EQ(Core_Register_Account_Public_Key_Notification, sub.notificationTypes()[2]);
			EXPECT_EQ(Core_Entity_Notification, sub.notificationTypes()[3]);
			EXPECT_EQ(Core_Block_Notification, sub.notificationTypes()[4]);
			EXPECT_EQ(Core_Signature_Notification, sub.notificationTypes()[5]);
		});
	}

	TEST(TEST_CLASS, CanPublishBlockNotificationsWithModeCustom) {
		// Arrange:
		auto pBlock = GenerateBlockWithTransactionSizes({});

		// Act:
		PublishAll(*pBlock, PublicationMode::Custom, [](const auto& sub) {
			// Assert: all notifications were suppressed (blocks do not have custom notifications)
			ASSERT_EQ(0u, sub.numNotifications());
		});
	}

	// endregion

	// region transaction

	TEST(TEST_CLASS, CanRaiseTransactionSourceChangeNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(0);

		// Act:
		PublishOne<SourceChangeNotification>(*pTransaction, [](const auto& notification) {
			// Assert:
			EXPECT_EQ(1u, notification.PrimaryId);
			EXPECT_EQ(0u, notification.SecondaryId);
			EXPECT_EQ(SourceChangeNotification::SourceChangeType::Relative, notification.PrimaryChangeType);
			EXPECT_EQ(SourceChangeNotification::SourceChangeType::Absolute, notification.SecondaryChangeType);
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionAccountNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(0);
		test::FillWithRandomData(pTransaction->SignerPublicKey);
		test::FillWithRandomData(pTransaction->RecipientPublicKey);

		// Act:
		PublishAll(*pTransaction, [&transaction = *pTransaction](const auto& sub) {
			// Assert: both signer (from notification publisher) and recipient (from custom publish implementation) are raised
			EXPECT_EQ(0u, sub.numAddresses());
			EXPECT_EQ(2u, sub.numKeys());

			EXPECT_TRUE(sub.contains(transaction.SignerPublicKey));
			EXPECT_TRUE(sub.contains(transaction.RecipientPublicKey));
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionEntityNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(0);
		pTransaction->Version = 0x5A;
		pTransaction->Network = static_cast<NetworkIdentifier>(0x11);

		// Act:
		PublishOne<EntityNotification>(*pTransaction, [](const auto& notification) {
			// Assert:
			EXPECT_EQ(static_cast<NetworkIdentifier>(0x11), notification.NetworkIdentifier);
			EXPECT_EQ(0x5Au, notification.EntityVersion);
			EXPECT_EQ(0x02u, notification.MinVersion); // from MockTransaction
			EXPECT_EQ(0xFEu, notification.MaxVersion);
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionSignatureNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);
		test::FillWithRandomData(pTransaction->SignerPublicKey);
		test::FillWithRandomData(pTransaction->Signature);

		// Act:
		PublishOne<SignatureNotification>(*pTransaction, [&transaction = *pTransaction](const auto& notification) {
			// Assert:
			EXPECT_EQ(transaction.SignerPublicKey, notification.SignerPublicKey);
			EXPECT_EQ(transaction.Signature, notification.Signature);

			// - notice that mock plugin is configured with PluginOptionFlags::Custom_Buffers so dataBuffer() contains only data payload
			EXPECT_EQ(test::AsVoidPointer(&transaction + 1), test::AsVoidPointer(notification.Data.pData));
			EXPECT_EQ(12u, notification.Data.Size);
			EXPECT_EQ(SignatureNotification::ReplayProtectionMode::Enabled, notification.DataReplayProtectionMode);
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionNotifications) {
		// Arrange:
		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pTransaction = mocks::CreateMockTransaction(12);
		test::FillWithRandomData(pTransaction->SignerPublicKey);
		pTransaction->Deadline = Timestamp(454);

		// Act:
		PublishOne<TransactionNotification>(*pTransaction, hash, [&signer = pTransaction->SignerPublicKey, &hash](
				const auto& notification) {
			// Assert:
			EXPECT_EQ(signer, notification.Sender);
			EXPECT_EQ(hash, notification.TransactionHash);
			EXPECT_EQ(static_cast<EntityType>(mocks::MockTransaction::Entity_Type), notification.TransactionType);
			EXPECT_EQ(Timestamp(454), notification.Deadline);
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionDeadlineNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);
		pTransaction->Deadline = Timestamp(454);

		// Act:
		PublishOne<TransactionDeadlineNotification>(*pTransaction, [](const auto& notification) {
			// Assert:
			EXPECT_EQ(Timestamp(454), notification.Deadline);
			EXPECT_EQ(utils::TimeSpan::FromMilliseconds(0xEEEE'EEEE'EEEE'1234), notification.MaxLifetime); // from MockTransaction
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionFeeNotification_BlockIndependent) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);
		pTransaction->MaxFee = Amount(765);

		// Act:
		PublishOne<TransactionFeeNotification>(*pTransaction, [&transaction = *pTransaction](const auto& notification) {
			// Assert: max fee is used when there is no associated block
			EXPECT_EQ(transaction.SignerPublicKey, notification.Sender);
			EXPECT_EQ(transaction.Size, notification.TransactionSize);
			EXPECT_EQ(Amount(765), notification.Fee);
			EXPECT_EQ(Amount(765), notification.MaxFee);
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionFeeNotification_BlockDependent) {
		// Arrange:
		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pTransaction = test::GenerateRandomTransactionWithSize(234);
		pTransaction->Type = mocks::MockTransaction::Entity_Type;
		pTransaction->MaxFee = Amount(765);

		BlockHeader blockHeader;
		blockHeader.FeeMultiplier = BlockFeeMultiplier(4);
		auto weakEntityInfo = WeakEntityInfo(*pTransaction, hash, blockHeader);

		// Act:
		PublishOne<TransactionFeeNotification>(weakEntityInfo, [&transaction = *pTransaction](const auto& notification) {
			// Assert: calculated fee is used when there is associated block
			EXPECT_EQ(transaction.SignerPublicKey, notification.Sender);
			EXPECT_EQ(transaction.Size, notification.TransactionSize);
			EXPECT_EQ(Amount(4 * 234), notification.Fee);
			EXPECT_EQ(Amount(765), notification.MaxFee);
		});
	}

	TEST(TEST_CLASS, CanRaiseTransactionFeeDebitNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);
		test::FillWithRandomData(pTransaction->SignerPublicKey);
		pTransaction->MaxFee = Amount(765);

		// Act:
		PublishOne<BalanceDebitNotification>(*pTransaction, [&signer = pTransaction->SignerPublicKey](const auto& notification) {
			// Assert:
			EXPECT_EQ(signer, notification.Sender);
			EXPECT_EQ(Currency_Mosaic_Id, notification.MosaicId);
			EXPECT_EQ(Amount(765), notification.Amount);
		});
	}

	namespace {
		void AssertCustomTransactionNotifications(const std::vector<NotificationType>& notificationTypes, size_t startIndex) {
			EXPECT_EQ(Core_Register_Account_Public_Key_Notification, notificationTypes[startIndex]);
			EXPECT_EQ(mocks::Mock_Address_Notification, notificationTypes[startIndex + 1]);
			EXPECT_EQ(mocks::Mock_Observer_1_Notification, notificationTypes[startIndex + 2]);
			EXPECT_EQ(mocks::Mock_Validator_1_Notification, notificationTypes[startIndex + 3]);
			EXPECT_EQ(mocks::Mock_All_1_Notification, notificationTypes[startIndex + 4]);
			EXPECT_EQ(mocks::Mock_Observer_2_Notification, notificationTypes[startIndex + 5]);
			EXPECT_EQ(mocks::Mock_Validator_2_Notification, notificationTypes[startIndex + 6]);
			EXPECT_EQ(mocks::Mock_All_2_Notification, notificationTypes[startIndex + 7]);
			EXPECT_EQ(mocks::Mock_Hash_Notification, notificationTypes[startIndex + 8]);
		}
	}

	TEST(TEST_CLASS, CanRaiseCustomTransactionNotifications) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);

		// Act:
		PublishAll(*pTransaction, [&transaction = *pTransaction](const auto& sub) {
			// Assert: 8 raised by NotificationPublisher, 9 raised by MockTransaction::publish
			ASSERT_EQ(8u + 9, sub.numNotifications());
			AssertCustomTransactionNotifications(sub.notificationTypes(), 8);
		});
	}

	TEST(TEST_CLASS, CanRaiseCustomTransactionNotificationsDependentOnHash) {
		// Arrange:
		auto hash = test::GenerateRandomByteArray<Hash256>();
		auto pTransaction = mocks::CreateMockTransaction(12);

		// Act:
		PublishOne<mocks::MockHashNotification>(*pTransaction, hash, [&hash](const auto& notification) {
			// Assert:
			EXPECT_EQ(&hash, &notification.Hash);
		});
	}

	TEST(TEST_CLASS, CanRaiseCustomTransactionNotificationsDependentOnSignerAddress) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);
		auto signerAddress = PublicKeyToAddress(pTransaction->SignerPublicKey, pTransaction->Network);

		// Act:
		PublishOne<mocks::MockAddressNotification>(*pTransaction, [&signerAddress](const auto& notification) {
			// Assert:
			EXPECT_EQ(signerAddress, notification.Address);
		});
	}

	TEST(TEST_CLASS, CanPublishTransactionNotificationsWithModeBasic) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);

		// Act:
		PublishAll(*pTransaction, PublicationMode::Basic, [&transaction = *pTransaction](const auto& sub) {
			// Assert: 8 raised by NotificationPublisher, none raised by MockTransaction::publish
			ASSERT_EQ(8u, sub.numNotifications());
			EXPECT_EQ(Core_Source_Change_Notification, sub.notificationTypes()[0]);
			EXPECT_EQ(Core_Register_Account_Public_Key_Notification, sub.notificationTypes()[1]);
			EXPECT_EQ(Core_Entity_Notification, sub.notificationTypes()[2]);
			EXPECT_EQ(Core_Transaction_Notification, sub.notificationTypes()[3]);
			EXPECT_EQ(Core_Transaction_Deadline_Notification, sub.notificationTypes()[4]);
			EXPECT_EQ(Core_Transaction_Fee_Notification, sub.notificationTypes()[5]);
			EXPECT_EQ(Core_Balance_Debit_Notification, sub.notificationTypes()[6]);
			EXPECT_EQ(Core_Signature_Notification, sub.notificationTypes()[7]);
		});
	}

	TEST(TEST_CLASS, CanPublishTransactionNotificationsWithModeCustom) {
		// Arrange:
		auto pTransaction = mocks::CreateMockTransaction(12);

		// Act:
		PublishAll(*pTransaction, PublicationMode::Custom, [&transaction = *pTransaction](const auto& sub) {
			// Assert: 9 raised by MockTransaction::publish
			ASSERT_EQ(9u, sub.numNotifications());
			AssertCustomTransactionNotifications(sub.notificationTypes(), 0);
		});
	}

	// endregion

	// region other

	TEST(TEST_CLASS, CannotRaiseAnyNotificationsForUnknownEntities) {
		// Arrange:
		VerifiableEntity entity{};

		// Act:
		EXPECT_THROW(PublishOne<SourceChangeNotification>(entity, [](const auto&) {}), catapult_runtime_error);
	}

	TEST(TEST_CLASS, CannotRaiseAnyNotificationsForUnknownEntitiesWithModeBasic) {
		// Arrange:
		VerifiableEntity entity{};

		// Act:
		EXPECT_THROW(PublishAll(entity, PublicationMode::Basic, [](const auto&) {}), catapult_runtime_error);
	}

	TEST(TEST_CLASS, CannotRaiseAnyNotificationsForUnknownEntitiesWithModeCustom) {
		// Arrange:
		VerifiableEntity entity{};

		// Act:
		EXPECT_THROW(PublishAll(entity, PublicationMode::Custom, [](const auto&) {}), catapult_runtime_error);
	}

	// endregion
}}
