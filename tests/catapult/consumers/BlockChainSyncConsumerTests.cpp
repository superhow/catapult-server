#include "catapult/consumers/BlockConsumers.h"
#include "catapult/cache/AccountStateCache.h"
#include "catapult/cache/BlockDifficultyCache.h"
#include "catapult/io/BlockStorageCache.h"
#include "catapult/model/ChainScore.h"
#include "tests/catapult/consumers/utils/ConsumerInputFactory.h"
#include "tests/catapult/consumers/utils/ConsumerTestUtils.h"
#include "tests/test/cache/CacheTestUtils.h"
#include "tests/test/core/BlockTestUtils.h"
#include "tests/test/core/mocks/MemoryBasedStorage.h"
#include "tests/test/nodeps/ParamsCapture.h"
#include "tests/TestHarness.h"

using catapult::disruptor::ConsumerInput;
using catapult::disruptor::InputSource;
using catapult::utils::HashSet;
using catapult::validators::ValidationResult;

namespace catapult { namespace consumers {

	namespace {
		constexpr auto Base_Difficulty = Difficulty().unwrap();
		constexpr model::ImportanceHeight Initial_Last_Recalculation_Height(1234);
		constexpr model::ImportanceHeight Modified_Last_Recalculation_Height(7777);
		const Key Sentinel_Processor_Public_Key = test::GenerateRandomData<Key_Size>();

		constexpr model::ImportanceHeight AddImportanceHeight(
				model::ImportanceHeight lhs,
				model::ImportanceHeight::ValueType rhs) {
			return model::ImportanceHeight(lhs.unwrap() + rhs);
		}

		// region MockDifficultyChecker

		struct DifficultyCheckerParams {
		public:
			DifficultyCheckerParams(
					const std::vector<const model::Block*>& blocks,
					const cache::CatapultCache& cache)
					: Blocks(blocks)
					, Cache(cache)
			{}

		public:
			const std::vector<const model::Block*> Blocks;
			const cache::CatapultCache& Cache;
		};

		class MockDifficultyChecker : public test::ParamsCapture<DifficultyCheckerParams> {
		public:
			MockDifficultyChecker() : m_result(true)
			{}

		public:
			bool operator()(const std::vector<const model::Block*>& blocks, const cache::CatapultCache& cache) const {
				const_cast<MockDifficultyChecker*>(this)->push(blocks, cache);
				return m_result;
			}

		public:
			void setFailure() {
				m_result = false;
			}

		private:
			bool m_result;
		};

		// endregion

		// region MockUndoBlock

		struct UndoBlockParams {
		public:
			UndoBlockParams(const model::BlockElement& blockElement, const observers::ObserverState& state)
					: pBlock(test::CopyBlock(blockElement.Block))
					, LastRecalculationHeight(state.State.LastRecalculationHeight)
					, IsPassedMarkedCache(test::IsMarkedCache(state.Cache))
					, NumDifficultyInfos(state.Cache.sub<cache::BlockDifficultyCache>().size())
			{}

		public:
			std::shared_ptr<const model::Block> pBlock;
			const model::ImportanceHeight LastRecalculationHeight;
			const bool IsPassedMarkedCache;
			const size_t NumDifficultyInfos;
		};

		class MockUndoBlock : public test::ParamsCapture<UndoBlockParams> {
		public:
			void operator()(const model::BlockElement& blockElement, const observers::ObserverState& state) const {
				const_cast<MockUndoBlock*>(this)->push(blockElement, state);

				// mark the state by modifying it
				auto& blockDifficultyCache = state.Cache.sub<cache::BlockDifficultyCache>();
				blockDifficultyCache.insert(state::BlockDifficultyInfo(Height(blockDifficultyCache.size() + 1)));

				auto& height = state.State.LastRecalculationHeight;
				height = AddImportanceHeight(height, 1);
			}
		};

		// endregion

		// region MockProcessor

		struct ProcessorParams {
		public:
			ProcessorParams(
					const WeakBlockInfo& parentBlockInfo,
					const BlockElements& elements,
					const observers::ObserverState& state)
					: pParentBlock(test::CopyBlock(parentBlockInfo.entity()))
					, ParentHash(parentBlockInfo.hash())
					, pElements(&elements)
					, LastRecalculationHeight(state.State.LastRecalculationHeight)
					, IsPassedMarkedCache(test::IsMarkedCache(state.Cache))
					, NumDifficultyInfos(state.Cache.sub<cache::BlockDifficultyCache>().size())
			{}

		public:
			std::shared_ptr<const model::Block> pParentBlock;
			const Hash256 ParentHash;
			const BlockElements* pElements;
			const model::ImportanceHeight LastRecalculationHeight;
			const bool IsPassedMarkedCache;
			const size_t NumDifficultyInfos;
		};

		class MockProcessor : public test::ParamsCapture<ProcessorParams> {
		public:
			MockProcessor() : m_result(ValidationResult::Success)
			{}

		public:
			ValidationResult operator()(
					const WeakBlockInfo& parentBlockInfo,
					BlockElements& elements,
					const observers::ObserverState& state) const {
				const_cast<MockProcessor*>(this)->push(parentBlockInfo, elements, state);

				// mark the state by modifying it
				state.Cache.sub<cache::AccountStateCache>().addAccount(Sentinel_Processor_Public_Key, Height(1));
				state.State.LastRecalculationHeight = Modified_Last_Recalculation_Height;

				// modify all the elements
				for (auto& element : elements)
					element.GenerationHash = { { static_cast<uint8_t>(element.Block.Height.unwrap()) } };

				return m_result;
			}

		public:
			void setResult(ValidationResult result) {
				m_result = result;
			}

		private:
			ValidationResult m_result;
		};

		// endregion

		// region MockStateChange

		struct StateChangeParams {
		public:
			StateChangeParams(const StateChangeInfo& changeInfo)
					: ScoreDelta(changeInfo.ScoreDelta)
					// all processing should have occurred before the state change notification,
					// so the sentinel account should have been added
					, IsPassedMarkedCache(changeInfo.CacheDelta.sub<cache::AccountStateCache>().contains(Sentinel_Processor_Public_Key))
					, Height(changeInfo.Height)
			{}

		public:
			model::ChainScore ScoreDelta;
			bool IsPassedMarkedCache;
			catapult::Height Height;
		};

		class MockStateChange : public test::ParamsCapture<StateChangeParams> {
		public:
			void operator()(const StateChangeInfo& changeInfo) const {
				const_cast<MockStateChange*>(this)->push(changeInfo);
			}
		};

		// endregion

		// region MockTransactionsChange

		struct TransactionsChangeParams {
		public:
			TransactionsChangeParams(
					const HashSet& addedTransactionHashes,
					const HashSet& revertedTransactionHashes)
					: AddedTransactionHashes(addedTransactionHashes)
					, RevertedTransactionHashes(revertedTransactionHashes)
			{}

		public:
			const HashSet AddedTransactionHashes;
			const HashSet RevertedTransactionHashes;
		};

		class MockTransactionsChange : public test::ParamsCapture<TransactionsChangeParams> {
		public:
			void operator()(
					const utils::HashPointerSet& addedTransactionHashes,
					std::vector<model::TransactionInfo>&& revertedTransactionInfos) const {

				TransactionsChangeParams params(CopyHashes(addedTransactionHashes), CopyHashes(revertedTransactionInfos));
				const_cast<MockTransactionsChange*>(this)->push(std::move(params));
			}

		private:
			static HashSet CopyHashes(const utils::HashPointerSet& hashPointers) {
				HashSet hashes;
				for (const auto* pHash : hashPointers)
					hashes.insert(*pHash);

				return hashes;
			}

			static HashSet CopyHashes(const std::vector<model::TransactionInfo>& transactionInfos) {
				HashSet hashes;
				for (const auto& info : transactionInfos)
					hashes.insert(info.EntityHash);

				return hashes;
			}
		};

		// endregion

		void SetBlockHeight(model::Block& block, Height height) {
			block.Timestamp = Timestamp(height.unwrap() * 1000);
			block.Difficulty = Difficulty();
			block.Height = height;
		}

		struct ConsumerTestContext {
		public:
			ConsumerTestContext()
					: Cache(test::CreateCatapultCacheWithMarkerAccount())
					, Storage(std::make_unique<mocks::MemoryBasedStorage>()) {
				State.LastRecalculationHeight = Initial_Last_Recalculation_Height;

				BlockChainSyncHandlers handlers;
				handlers.DifficultyChecker = [this](const auto& blocks, const auto& cache) {
					return DifficultyChecker(blocks, cache);
				};
				handlers.UndoBlock = [this](const auto& block, const auto& state) {
					return UndoBlock(block, state);
				};
				handlers.Processor = [this](const auto& parentBlockInfo, auto& elements, const auto& cache) {
					return Processor(parentBlockInfo, elements, cache);
				};
				handlers.StateChange = [this](const auto& changeInfo) {
					return StateChange(changeInfo);
				};
				handlers.TransactionsChange = [this](const auto& addedTransactionHashes, auto&& revertedTransactionInfos) {
					return TransactionsChange(addedTransactionHashes, std::move(revertedTransactionInfos));
				};

				Consumer = CreateBlockChainSyncConsumer(Cache, State, Storage, handlers);
			}

		public:
			cache::CatapultCache Cache;
			state::CatapultState State;
			io::BlockStorageCache Storage;
			std::vector<std::shared_ptr<model::Block>> OriginalBlocks; // original stored blocks (excluding nemesis)

			MockDifficultyChecker DifficultyChecker;
			MockUndoBlock UndoBlock;
			MockProcessor Processor;
			MockStateChange StateChange;
			MockTransactionsChange TransactionsChange;

			disruptor::DisruptorConsumer Consumer;

		public:
			void seedStorage(Height desiredHeight, size_t numTransactionsPerBlock = 0) {
				// Arrange:
				auto height = Storage.view().chainHeight();
				auto storageModifier = Storage.modifier();

				while (height < desiredHeight) {
					height = height + Height(1);

					auto transactions = test::GenerateRandomTransactions(numTransactionsPerBlock);
					auto pBlock = test::GenerateRandomBlockWithTransactions(transactions);
					SetBlockHeight(*pBlock, height);

					// - seed with random tx hashes
					auto blockElement = test::BlockToBlockElement(*pBlock);
					for (auto& txElement : blockElement.Transactions)
						txElement.EntityHash = test::GenerateRandomData<Hash256_Size>();

					storageModifier.saveBlock(blockElement);
					OriginalBlocks.push_back(std::move(pBlock));
				}
			}

		public:
			void assertDifficultyCheckerInvocation(const ConsumerInput& input) {
				// Assert:
				ASSERT_EQ(1u, DifficultyChecker.params().size());
				auto difficultyParams = DifficultyChecker.params()[0];

				EXPECT_EQ(&Cache, &difficultyParams.Cache);
				ASSERT_EQ(input.blocks().size(), difficultyParams.Blocks.size());
				for (auto i = 0u; i < input.blocks().size(); ++i)
					EXPECT_EQ(&input.blocks()[i].Block, difficultyParams.Blocks[i]) << "block at " << i;
			}

			void assertUnwind(const std::vector<Height>& unwoundHeights) {
				// Assert:
				ASSERT_EQ(unwoundHeights.size(), UndoBlock.params().size());
				auto i = 0u;
				for (auto height : unwoundHeights) {
					const auto& undoBlockParams = UndoBlock.params()[i];
					auto expectedHeight = AddImportanceHeight(Initial_Last_Recalculation_Height, i);

					EXPECT_EQ(*OriginalBlocks[(height - Height(2)).unwrap()], *undoBlockParams.pBlock) << "undo at " << i;
					EXPECT_EQ(expectedHeight, undoBlockParams.LastRecalculationHeight) << "undo at " << i;
					EXPECT_TRUE(undoBlockParams.IsPassedMarkedCache) << "undo at " << i;
					EXPECT_EQ(i, undoBlockParams.NumDifficultyInfos) << "undo at " << i;
					++i;
				}
			}

			void assertProcessorInvocation(const ConsumerInput& input, size_t numUnwoundBlocks = 0) {
				// Assert:
				ASSERT_EQ(1u, Processor.params().size());
				const auto& processorParams = Processor.params()[0];
				auto expectedHeight = AddImportanceHeight(Initial_Last_Recalculation_Height, numUnwoundBlocks);
				auto pCommonBlockElement = Storage.view().loadBlockElement(input.blocks()[0].Block.Height - Height(1));

				EXPECT_EQ(pCommonBlockElement->Block, *processorParams.pParentBlock);
				EXPECT_EQ(pCommonBlockElement->EntityHash, processorParams.ParentHash);
				EXPECT_EQ(&input.blocks(), processorParams.pElements);
				EXPECT_EQ(expectedHeight, processorParams.LastRecalculationHeight);
				EXPECT_TRUE(processorParams.IsPassedMarkedCache);
				EXPECT_EQ(numUnwoundBlocks, processorParams.NumDifficultyInfos);
			}

			void assertNoStorageChanges() {
				// Assert: all original blocks are present in the storage
				auto storageView = Storage.view();
				ASSERT_EQ(Height(OriginalBlocks.size()) + Height(1), storageView.chainHeight());
				for (const auto& pBlock : OriginalBlocks) {
					auto pStorageBlock = storageView.loadBlock(pBlock->Height);
					EXPECT_EQ(*pBlock, *pStorageBlock) << "at height " << pBlock->Height;
				}

				// - the cache was not committed
				EXPECT_FALSE(Cache.sub<cache::AccountStateCache>().createView()->contains(Sentinel_Processor_Public_Key));
				EXPECT_EQ(0u, Cache.sub<cache::BlockDifficultyCache>().createView()->size());

				// - no state changes were announced
				EXPECT_EQ(0u, StateChange.params().size());

				// - no transaction changes were announced
				EXPECT_EQ(0u, TransactionsChange.params().size());

				// - the state was not changed
				EXPECT_EQ(Initial_Last_Recalculation_Height, State.LastRecalculationHeight);
			}

			void assertStored(const ConsumerInput& input, const model::ChainScore& expectedScoreDelta) {
				// Assert: all input blocks should be saved in the storage
				auto storageView = Storage.view();
				auto inputHeight = input.blocks()[0].Block.Height;
				auto chainHeight = storageView.chainHeight();
				ASSERT_EQ(inputHeight + Height(input.blocks().size() - 1), chainHeight);
				for (auto height = inputHeight; height <= chainHeight; height = height + Height(1)) {
					auto pStorageBlock = storageView.loadBlock(height);
					EXPECT_EQ(input.blocks()[(height - inputHeight).unwrap()].Block, *pStorageBlock)
							<< "at height " << height;
				}

				// - non conflicting original blocks should still be in storage
				for (auto height = Height(2); height < inputHeight; height = height + Height(1)) {
					auto pStorageBlock = storageView.loadBlock(height);
					EXPECT_EQ(*OriginalBlocks[(height - Height(2)).unwrap()], *pStorageBlock) << "at height " << height;
				}

				// - the cache was committed (add 1 to OriginalBlocks.size() because it does not include the nemesis)
				EXPECT_TRUE(Cache.sub<cache::AccountStateCache>().createView()->contains(Sentinel_Processor_Public_Key));
				EXPECT_EQ(
						OriginalBlocks.size() + 1 - inputHeight.unwrap() + 1,
						Cache.sub<cache::BlockDifficultyCache>().createView()->size());
				EXPECT_EQ(chainHeight, Cache.createView().height());

				// - the state was changed
				ASSERT_EQ(1u, StateChange.params().size());
				const auto& stateChangeParams = StateChange.params()[0];
				EXPECT_EQ(expectedScoreDelta, stateChangeParams.ScoreDelta);
				EXPECT_TRUE(stateChangeParams.IsPassedMarkedCache);
				EXPECT_EQ(chainHeight, stateChangeParams.Height);

				// - transaction changes were announced
				EXPECT_EQ(1u, TransactionsChange.params().size());

				// - the state was changed
				EXPECT_EQ(Modified_Last_Recalculation_Height, State.LastRecalculationHeight);
			}
		};
	}

	TEST(BlockChainSyncConsumerTests, CanProcessZeroEntities) {
		// Arrange:
		ConsumerTestContext context;

		// Assert:
		test::AssertPassthroughForEmptyInput(context.Consumer);
	}

	namespace {
		std::vector<InputSource> GetAllInputSources() {
			return { InputSource::Unknown, InputSource::Local, InputSource::Remote_Pull, InputSource::Remote_Push };
		}

		void LogInputSource(InputSource source) {
			CATAPULT_LOG(debug) << "source " << source;
		}

		ConsumerInput CreateInput(Height startHeight, uint32_t numBlocks, InputSource source = InputSource::Remote_Pull) {
			auto input = test::CreateConsumerInputWithBlocks(numBlocks, source);
			auto nextHeight = startHeight;
			for (const auto& element : input.blocks()) {
				SetBlockHeight(const_cast<model::Block&>(element.Block), nextHeight);
				nextHeight = nextHeight + Height(1);
			}

			return input;
		}

		void AssertInvalidHeight(Height localHeight, Height remoteHeight, uint32_t numRemoteBlocks, InputSource source) {
			// Arrange:
			ConsumerTestContext context;
			context.seedStorage(localHeight);
			auto input = CreateInput(remoteHeight, numRemoteBlocks, source);

			// Act:
			auto result = context.Consumer(input);

			// Assert:
			test::AssertAborted(result, Failure_Consumer_Remote_Chain_Unlinked);
			EXPECT_EQ(0u, context.DifficultyChecker.params().size());
			EXPECT_EQ(0u, context.UndoBlock.params().size());
			EXPECT_EQ(0u, context.Processor.params().size());
			context.assertNoStorageChanges();
		}

		void AssertValidHeight(Height localHeight, Height remoteHeight, uint32_t numRemoteBlocks, InputSource source) {
			// Arrange:
			ConsumerTestContext context;
			context.seedStorage(localHeight);
			auto input = CreateInput(remoteHeight, numRemoteBlocks, source);

			// Act:
			context.Consumer(input);

			// Assert: if the height is valid, the difficulty checker must have been called
			EXPECT_EQ(1u, context.DifficultyChecker.params().size());
		}
	}

	// region height check

	TEST(BlockChainSyncConsumerTests, RemoteChainWithHeightLessThanTwoIsRejected) {
		// Assert:
		for (auto source : GetAllInputSources()) {
			LogInputSource(source);
			AssertInvalidHeight(Height(1), Height(0), 3, source);
			AssertInvalidHeight(Height(1), Height(1), 3, source);
		}
	}

	TEST(BlockChainSyncConsumerTests, RemoteChainWithHeightAtLeastTwoIsValid) {
		// Assert:
		for (auto source : GetAllInputSources()) {
			LogInputSource(source);
			AssertValidHeight(Height(1), Height(2), 3, source);
			AssertValidHeight(Height(2), Height(3), 3, source);
		}
	}

	TEST(BlockChainSyncConsumerTests, RemoteChainWithHeightMoreThanOneGreaterThanLocalHeightIsRejected) {
		// Assert:
		for (auto source : GetAllInputSources()) {
			LogInputSource(source);
			AssertInvalidHeight(Height(100), Height(102), 3, source);
			AssertInvalidHeight(Height(100), Height(200), 3, source);
		}
	}

	TEST(BlockChainSyncConsumerTests, RemoteChainWithHeightLessThanLocalHeightIsOnlyValidForRemotePullSource) {
		// Assert:
		for (auto source : GetAllInputSources()) {
			LogInputSource(source);
			auto assertFunc = InputSource::Remote_Pull == source ? AssertValidHeight : AssertInvalidHeight;
			assertFunc(Height(100), Height(99), 1, source);
			assertFunc(Height(100), Height(90), 1, source);
		}
	}

	TEST(BlockChainSyncConsumerTests, RemoteChainWithHeightAtOrOneGreaterThanLocalHeightIsValidForAllSources) {
		// Assert:
		for (auto source : GetAllInputSources()) {
			LogInputSource(source);
			AssertValidHeight(Height(100), Height(100), 1, source);
			AssertValidHeight(Height(100), Height(101), 1, source);
		}
	}

	// endregion

	// region difficulties check

	TEST(BlockChainSyncConsumerTests, RemoteChainWithIncorrectDifficultiesIsRejected) {
		// Arrange: trigger a difficulty check failure
		ConsumerTestContext context;
		context.seedStorage(Height(3));
		context.DifficultyChecker.setFailure();

		auto input = CreateInput(Height(4), 2);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertAborted(result, Failure_Consumer_Remote_Chain_Mismatched_Difficulties);
		EXPECT_EQ(0u, context.UndoBlock.params().size());
		EXPECT_EQ(0u, context.Processor.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertNoStorageChanges();
	}

	// endregion

	// region chain score test

	TEST(BlockChainSyncConsumerTests, ChainWithSmallerScoreIsRejected) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 5-6
		//          (note that the test setup ensures scores are linearly correlated with number of blocks)
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(5), 2);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertAborted(result, Failure_Consumer_Remote_Chain_Score_Not_Better);
		EXPECT_EQ(3u, context.UndoBlock.params().size());
		EXPECT_EQ(0u, context.Processor.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7), Height(6), Height(5) });
		context.assertNoStorageChanges();
	}

	TEST(BlockChainSyncConsumerTests, ChainWithIdenticalScoreIsRejected) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 6-7
		//          (note that the test setup ensures scores are linearly correlated with number of blocks)
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(6), 2);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertAborted(result, Failure_Consumer_Remote_Chain_Score_Not_Better);
		EXPECT_EQ(2u, context.UndoBlock.params().size());
		EXPECT_EQ(0u, context.Processor.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7), Height(6) });
		context.assertNoStorageChanges();
	}

	// endregion

	// region processor check

	namespace {
		void AssertRemoteChainWithNonSuccessProcessorResultIsRejected(ValidationResult processorResult) {
			// Arrange: configure the processor to return a non-success result
			ConsumerTestContext context;
			context.seedStorage(Height(3));
			context.Processor.setResult(processorResult);

			auto input = CreateInput(Height(4), 2);

			// Act:
			auto result = context.Consumer(input);

			// Assert:
			test::AssertAborted(result, processorResult);
			EXPECT_EQ(0u, context.UndoBlock.params().size());
			context.assertDifficultyCheckerInvocation(input);
			context.assertProcessorInvocation(input);
			context.assertNoStorageChanges();
		}
	}

	TEST(BlockChainSyncConsumerTests, RemoteChainWithProcessorFailureIsRejected_Neutral) {
		// Assert:
		AssertRemoteChainWithNonSuccessProcessorResultIsRejected(ValidationResult::Neutral);
	}

	TEST(BlockChainSyncConsumerTests, RemoteChainWithProcessorFailureIsRejected_Failure) {
		// Assert:
		AssertRemoteChainWithNonSuccessProcessorResultIsRejected(ValidationResult::Failure);
	}

	// endregion

	// region successful syncs

	TEST(BlockChainSyncConsumerTests, CanSyncCompatibleChains) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 8-11
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(8), 4);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(0u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertProcessorInvocation(input);
		context.assertStored(input, model::ChainScore(4 * (Base_Difficulty - 1)));
	}

	TEST(BlockChainSyncConsumerTests, CanSyncIncompatibleChains) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 5-8
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(5), 4);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(3u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7), Height(6), Height(5) });
		context.assertProcessorInvocation(input, 3);
		context.assertStored(input, model::ChainScore(Base_Difficulty - 1));
	}

	TEST(BlockChainSyncConsumerTests, CanSyncIncompatibleChainsWithOnlyLastBlockDifferent) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 7-10
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(7), 4);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(1u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7) });
		context.assertProcessorInvocation(input, 1);
		context.assertStored(input, model::ChainScore(3 * (Base_Difficulty - 1)));
	}

	TEST(BlockChainSyncConsumerTests, CanSyncIncompatibleChainsWhereShorterRemoteChainHasHigherScore) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 5
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(5), 1);
		const_cast<model::Block&>(input.blocks()[0].Block).Difficulty = Difficulty(Base_Difficulty * 3);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(3u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7), Height(6), Height(5) });
		context.assertProcessorInvocation(input, 3);
		context.assertStored(input, model::ChainScore(2));
	}

	// endregion

	// region transaction notification

	namespace {
		template<typename TContainer, typename TKey>
		bool Contains(const TContainer& container, const TKey& key) {
			return container.cend() != container.find(key);
		}

		void AssertHashesAreEqual(const std::vector<Hash256>& expected, const HashSet& actual) {
			EXPECT_EQ(expected.size(), actual.size());

			auto i = 0u;
			for (const auto& hash : expected)
				EXPECT_TRUE(Contains(actual, hash)) << "hash at " << i++;
		}

		class InputTransactionBuilder {
		public:
			explicit InputTransactionBuilder(ConsumerInput& input) : m_input(input)
			{}

		public:
			const std::vector<Hash256>& hashes() const {
				return m_addedHashes;
			}

		public:
			void addRandom(size_t elementIndex, size_t numTransactions) {
				for (auto i = 0u; i < numTransactions; ++i)
					add(elementIndex, test::GenerateRandomTransaction(), test::GenerateRandomData<Hash256_Size>());
			}

			void addFromStorage(size_t elementIndex, const io::BlockStorageCache& storage, Height height, size_t txIndex) {
				auto pBlockElement = storage.view().loadBlockElement(height);

				auto i = 0u;
				for (const auto& txElement : pBlockElement->Transactions) {
					if (i++ != txIndex)
						continue;

					add(elementIndex, test::CopyTransaction(txElement.Transaction), txElement.EntityHash);
					break;
				}
			}

		private:
			void add(size_t elementIndex, const std::shared_ptr<model::Transaction>& pTransaction, const Hash256& hash) {
				auto txElement = model::TransactionElement(*pTransaction);
				txElement.EntityHash = hash;

				m_input.blocks()[elementIndex].Transactions.push_back(txElement);
				m_addedHashes.push_back(hash);
				m_transactions.push_back(pTransaction); // keep the transaction alive
			}

		private:
			ConsumerInput& m_input;
			std::vector<Hash256> m_addedHashes;
			std::vector<std::shared_ptr<model::Transaction>> m_transactions;
		};

		std::vector<Hash256> ExtractTransactionHashesFromStorage(
				const io::BlockStorageView& storage,
				Height startHeight,
				Height endHeight) {
			std::vector<Hash256> hashes;
			for (auto h = startHeight; h <= endHeight; h = h + Height(1)) {
				auto pBlockElement = storage.loadBlockElement(h);
				for (const auto& transactionElement : pBlockElement->Transactions)
					hashes.push_back(transactionElement.EntityHash);
			}

			return hashes;
		}
	}

	TEST(BlockChainSyncConsumerTests, CanSyncCompatibleChains_TransactionNotification) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 8-11
		ConsumerTestContext context;
		context.seedStorage(Height(7), 3);
		auto input = CreateInput(Height(8), 4);

		// - add transactions to the input
		InputTransactionBuilder builder(input);
		builder.addRandom(0, 1);
		builder.addRandom(2, 3);
		builder.addRandom(3, 2);

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(0u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertProcessorInvocation(input);
		context.assertStored(input, model::ChainScore(4 * (Base_Difficulty - 1)));

		// - the change notification had 6 added and 0 reverted
		ASSERT_EQ(1u, context.TransactionsChange.params().size());
		const auto& txChangeParams = context.TransactionsChange.params()[0];

		EXPECT_EQ(6u, txChangeParams.AddedTransactionHashes.size());
		AssertHashesAreEqual(builder.hashes(), txChangeParams.AddedTransactionHashes);

		EXPECT_TRUE(txChangeParams.RevertedTransactionHashes.empty());
	}

	TEST(BlockChainSyncConsumerTests, CanSyncIncompatibleChains_TransactionNotification) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 5-8
		ConsumerTestContext context;
		context.seedStorage(Height(7), 3);
		auto input = CreateInput(Height(5), 4);

		// - add transactions to the input
		InputTransactionBuilder builder(input);
		builder.addRandom(0, 1);
		builder.addRandom(2, 3);
		builder.addRandom(3, 2);

		// - extract original hashes from storage
		auto expectedRevertedHashes = ExtractTransactionHashesFromStorage(context.Storage.view(), Height(5), Height(7));

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(3u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7), Height(6), Height(5) });
		context.assertProcessorInvocation(input, 3);
		context.assertStored(input, model::ChainScore(Base_Difficulty - 1));

		// - the change notification had 6 added and 9 reverted
		ASSERT_EQ(1u, context.TransactionsChange.params().size());
		const auto& txChangeParams = context.TransactionsChange.params()[0];

		EXPECT_EQ(6u, txChangeParams.AddedTransactionHashes.size());
		AssertHashesAreEqual(builder.hashes(), txChangeParams.AddedTransactionHashes);

		EXPECT_EQ(9u, txChangeParams.RevertedTransactionHashes.size());
		AssertHashesAreEqual(expectedRevertedHashes, txChangeParams.RevertedTransactionHashes);
	}

	TEST(BlockChainSyncConsumerTests, CanSyncIncompatibleChainsWithSharedTransacions_TransactionNotification) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 5-8
		ConsumerTestContext context;
		context.seedStorage(Height(7), 3);
		auto input = CreateInput(Height(5), 4);

		// - add transactions to the input
		InputTransactionBuilder builder(input);
		builder.addRandom(0, 1);
		builder.addRandom(2, 3);
		builder.addRandom(3, 2);
		builder.addFromStorage(2, context.Storage, Height(5), 2);
		builder.addFromStorage(0, context.Storage, Height(7), 1);

		// - extract original hashes from storage
		auto expectedRevertedHashes = ExtractTransactionHashesFromStorage(context.Storage.view(), Height(5), Height(7));
		expectedRevertedHashes.erase(expectedRevertedHashes.begin() + 2 * 3 + 1); // block 7 tx 2
		expectedRevertedHashes.erase(expectedRevertedHashes.begin() + 2); // block 5 tx 3

		// Act:
		auto result = context.Consumer(input);

		// Assert:
		test::AssertContinued(result);
		EXPECT_EQ(3u, context.UndoBlock.params().size());
		context.assertDifficultyCheckerInvocation(input);
		context.assertUnwind({ Height(7), Height(6), Height(5) });
		context.assertProcessorInvocation(input, 3);
		context.assertStored(input, model::ChainScore(Base_Difficulty - 1));

		// - the change notification had 8 added and 7 reverted
		ASSERT_EQ(1u, context.TransactionsChange.params().size());
		const auto& txChangeParams = context.TransactionsChange.params()[0];

		EXPECT_EQ(8u, txChangeParams.AddedTransactionHashes.size());
		AssertHashesAreEqual(builder.hashes(), txChangeParams.AddedTransactionHashes);

		EXPECT_EQ(7u, txChangeParams.RevertedTransactionHashes.size());
		AssertHashesAreEqual(expectedRevertedHashes, txChangeParams.RevertedTransactionHashes);
	}

	// endregion

	// region element updates

	TEST(BlockChainSyncConsumerTests, AllowsUpdateOfInputElements) {
		// Arrange: create a local storage with blocks 1-7 and a remote storage with blocks 8-11
		ConsumerTestContext context;
		context.seedStorage(Height(7));
		auto input = CreateInput(Height(8), 4);

		// Sanity: clear all generation hashes
		for (auto& blockElement : input.blocks())
			blockElement.GenerationHash = {};

		// Act:
		auto result = context.Consumer(input);

		// Sanity:
		test::AssertContinued(result);

		// Assert: the input generation hashes were updated
		uint8_t i = 8;
		for (const auto& blockElement : input.blocks()) {
			Hash256 expectedGenerationHash{ { i++ } };
			EXPECT_EQ(expectedGenerationHash, blockElement.GenerationHash) << "generation hash at " << i;
		}
	}

	// endregion
}}