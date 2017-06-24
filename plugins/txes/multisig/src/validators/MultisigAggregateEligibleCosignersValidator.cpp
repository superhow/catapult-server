#include "Validators.h"
#include "src/cache/MultisigCache.h"
#include "catapult/utils/HashSet.h"
#include "catapult/validators/ValidatorContext.h"

namespace catapult { namespace validators {

	using Notification = model::AggregateCosignaturesNotification;
	// Key and Hash256 have same underlying type
	using KeyPointerFlagMap = std::unordered_map<const Hash256*, bool, utils::Hash256PointerHasher, utils::Hash256PointerEquality>;

	namespace {
		const model::EmbeddedEntity* AdvanceNext(const model::EmbeddedEntity* pTransaction) {
			return reinterpret_cast<const model::EmbeddedEntity*>(reinterpret_cast<const uint8_t*>(pTransaction) + pTransaction->Size);
		}

		class AggregateCosignaturesChecker {
		public:
			explicit AggregateCosignaturesChecker(
					const Notification& notification,
					const cache::MultisigCache::CacheReadOnlyType& multisigCache)
					: m_notification(notification)
					, m_multisigCache(multisigCache) {
				m_cosigners.emplace(&m_notification.Signer, false);
				for (auto i = 0u; i < m_notification.CosignaturesCount; ++i)
					m_cosigners.emplace(&m_notification.CosignaturesPtr[i].Signer, false);
			}

		public:
			bool hasIneligibleCosigners() {
				// find all eligible cosigners
				const auto* pTransaction = m_notification.TransactionsPtr;
				for (auto i = 0u; i < m_notification.TransactionsCount; ++i) {
					findEligibleCosigners(pTransaction->Signer);
					if (model::EntityType::Modify_Multisig_Account == pTransaction->Type) {
						const auto& multisigModify = static_cast<const model::EmbeddedModifyMultisigAccountTransaction&>(*pTransaction);
						findEligibleCosigners(multisigModify);
					}

					pTransaction = AdvanceNext(pTransaction);
				}

				// check if all cosigners are in fact eligible
				return std::any_of(m_cosigners.cbegin(), m_cosigners.cend(), [](const auto& pair) {
					return !pair.second;
				});
			}

		private:
			void findEligibleCosigners(const Key& publicKey) {
				// if the account is unknown or not multisig, only the public key itself is eligible
				if (!m_multisigCache.contains(publicKey)) {
					markEligible(publicKey);
					return;
				}

				// if the account is a cosignatory only, treat it as non-multisig
				const auto& multisigEntry = m_multisigCache.get(publicKey);
				if (multisigEntry.cosignatories().empty()) {
					markEligible(publicKey);
					return;
				}

				// if the account is multisig, only its cosignatories are eligible
				for (const auto& cosignatoryPublicKey : multisigEntry.cosignatories())
					findEligibleCosigners(cosignatoryPublicKey);
			}

			void findEligibleCosigners(const model::EmbeddedModifyMultisigAccountTransaction& transaction) {
				// since AggregateCosignaturesNotification is the first notification raised by an aggregate,
				// treat initial and added cosignatories as eligible
				const auto* pModification = transaction.ModificationsPtr();
				for (auto i = 0u; i < transaction.ModificationsCount; ++i) {
					if (model::CosignatoryModificationType::Add == pModification->ModificationType)
						markEligible(pModification->CosignatoryPublicKey);

					++pModification;
				}
			}

			void markEligible(const Key& key) {
				auto iter = m_cosigners.find(&key);
				if (m_cosigners.cend() != iter)
					iter->second = true;
			}

		private:
			const Notification& m_notification;
			const cache::MultisigCache::CacheReadOnlyType& m_multisigCache;
			KeyPointerFlagMap m_cosigners;
		};
	}

	stateful::NotificationValidatorPointerT<Notification> CreateMultisigAggregateEligibleCosignersValidator() {
		return std::make_unique<stateful::FunctionalNotificationValidatorT<Notification>>(
				"MultisigAggregateEligibleCosignersValidator",
				[](const auto& notification, const ValidatorContext& context) {
					AggregateCosignaturesChecker checker(notification, context.Cache.sub<cache::MultisigCache>());
					return checker.hasIneligibleCosigners() ? Failure_Multisig_Ineligible_Cosigners : ValidationResult::Success;
				});
	}
}}