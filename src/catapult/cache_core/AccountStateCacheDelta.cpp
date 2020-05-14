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

#include "AccountStateCacheDelta.h"
#include "catapult/model/Address.h"
#include "catapult/utils/Casting.h"
#include "catapult/utils/HexFormatter.h"
#include "catapult/functions.h"

namespace catapult { namespace cache {

	BasicAccountStateCacheDelta::BasicAccountStateCacheDelta(
			const AccountStateCacheTypes::BaseSetDeltaPointers& accountStateSets,
			const AccountStateCacheTypes::Options& options,
			const HighValueAccounts& highValueAccounts)
			: BasicAccountStateCacheDelta(
					accountStateSets,
					options,
					highValueAccounts,
					std::make_unique<AccountStateCacheDeltaMixins::KeyLookupAdapter>(
							*accountStateSets.pKeyLookupMap,
							*accountStateSets.pPrimary))
	{}

	BasicAccountStateCacheDelta::BasicAccountStateCacheDelta(
			const AccountStateCacheTypes::BaseSetDeltaPointers& accountStateSets,
			const AccountStateCacheTypes::Options& options,
			const HighValueAccounts& highValueAccounts,
			std::unique_ptr<AccountStateCacheDeltaMixins::KeyLookupAdapter>&& pKeyLookupAdapter)
			: AccountStateCacheDeltaMixins::Size(*accountStateSets.pPrimary)
			, AccountStateCacheDeltaMixins::ContainsAddress(*accountStateSets.pPrimary)
			, AccountStateCacheDeltaMixins::ContainsKey(*accountStateSets.pKeyLookupMap)
			, AccountStateCacheDeltaMixins::ConstAccessorAddress(*accountStateSets.pPrimary)
			, AccountStateCacheDeltaMixins::ConstAccessorKey(*pKeyLookupAdapter)
			, AccountStateCacheDeltaMixins::MutableAccessorAddress(*accountStateSets.pPrimary)
			, AccountStateCacheDeltaMixins::MutableAccessorKey(*pKeyLookupAdapter)
			, AccountStateCacheDeltaMixins::PatriciaTreeDelta(*accountStateSets.pPrimary, accountStateSets.pPatriciaTree)
			, AccountStateCacheDeltaMixins::DeltaElements(*accountStateSets.pPrimary)
			, m_pStateByAddress(accountStateSets.pPrimary)
			, m_pKeyToAddress(accountStateSets.pKeyLookupMap)
			, m_options(options)
			, m_highValueAccounts(highValueAccounts)
			, m_pKeyLookupAdapter(std::move(pKeyLookupAdapter))
			, m_highValueAccountsUpdater(m_options, highValueAccounts.addresses())
	{}

	model::NetworkIdentifier BasicAccountStateCacheDelta::networkIdentifier() const {
		return m_options.NetworkIdentifier;
	}

	uint64_t BasicAccountStateCacheDelta::importanceGrouping() const {
		return m_options.ImportanceGrouping;
	}

	Amount BasicAccountStateCacheDelta::minHarvesterBalance() const {
		return m_options.MinHarvesterBalance;
	}

	Amount BasicAccountStateCacheDelta::maxHarvesterBalance() const {
		return m_options.MaxHarvesterBalance;
	}

	MosaicId BasicAccountStateCacheDelta::harvestingMosaicId() const {
		return m_options.HarvestingMosaicId;
	}

	Address BasicAccountStateCacheDelta::getAddress(const Key& publicKey) {
		auto keyToAddressIter = m_pKeyToAddress->find(publicKey);
		const auto* pPair = keyToAddressIter.get();
		if (pPair)
			return pPair->second;

		auto address = model::PublicKeyToAddress(publicKey, m_options.NetworkIdentifier);
		m_pKeyToAddress->emplace(publicKey, address);
		return address;
	}

	void BasicAccountStateCacheDelta::addAccount(const Address& address, Height height) {
		if (contains(address))
			return;

		addAccount(state::AccountState(address, height));
	}

	void BasicAccountStateCacheDelta::addAccount(const Key& publicKey, Height height) {
		auto address = getAddress(publicKey);
		addAccount(address, height);

		// optimize common case where public key is already known by not marking account as dirty in that case
		auto accountStateIterConst = const_cast<const BasicAccountStateCacheDelta*>(this)->find(address);
		auto& accountStateConst = accountStateIterConst.get();
		if (Height(0) != accountStateConst.PublicKeyHeight)
			return;

		auto accountStateIter = this->find(address);
		auto& accountState = accountStateIter.get();
		accountState.PublicKey = publicKey;
		accountState.PublicKeyHeight = height;
	}

	void BasicAccountStateCacheDelta::addAccount(const state::AccountState& accountState) {
		if (contains(accountState.Address))
			return;

		if (Height(0) != accountState.PublicKeyHeight)
			m_pKeyToAddress->emplace(accountState.PublicKey, accountState.Address);

		m_pStateByAddress->insert(accountState);
		m_pStateByAddress->find(accountState.Address).get()->Balances.optimize(m_options.CurrencyMosaicId);
	}

	void BasicAccountStateCacheDelta::remove(const Address& address, Height height) {
		auto accountStateIter = this->find(address);
		if (!accountStateIter.tryGet())
			return;

		const auto& accountState = accountStateIter.get();
		if (height != accountState.AddressHeight)
			return;

		// note: we can only remove the entry from m_pKeyToAddress if the account state's public key is valid
		if (Height(0) != accountState.PublicKeyHeight)
			m_pKeyToAddress->remove(accountState.PublicKey);

		m_pStateByAddress->remove(address);
	}

	void BasicAccountStateCacheDelta::remove(const Key& publicKey, Height height) {
		auto accountStateIter = this->find(publicKey);
		if (!accountStateIter.tryGet())
			return;

		auto& accountState = accountStateIter.get();
		if (height != accountState.PublicKeyHeight)
			return;

		m_pKeyToAddress->remove(accountState.PublicKey);

		// if same height, remove address entry too
		if (accountState.PublicKeyHeight == accountState.AddressHeight) {
			m_pStateByAddress->remove(accountState.Address);
			return;
		}

		// safe, as the account is still in m_pStateByAddress
		accountState.PublicKeyHeight = Height(0);
		accountState.PublicKey = Key();
	}

	void BasicAccountStateCacheDelta::queueRemove(const Address& address, Height height) {
		m_queuedRemoveByAddress.emplace(height, address);
	}

	void BasicAccountStateCacheDelta::queueRemove(const Key& publicKey, Height height) {
		m_queuedRemoveByPublicKey.emplace(height, publicKey);
	}

	void BasicAccountStateCacheDelta::clearRemove(const Address& address, Height height) {
		m_queuedRemoveByAddress.erase(std::make_pair(height, address));
	}

	void BasicAccountStateCacheDelta::clearRemove(const Key& publicKey, Height height) {
		m_queuedRemoveByPublicKey.erase(std::make_pair(height, publicKey));
	}

	void BasicAccountStateCacheDelta::commitRemovals() {
		for (const auto& addressHeightPair : m_queuedRemoveByAddress)
			remove(addressHeightPair.second, addressHeightPair.first);

		for (const auto& keyHeightPair : m_queuedRemoveByPublicKey)
			remove(keyHeightPair.second, keyHeightPair.first);

		m_queuedRemoveByAddress.clear();
		m_queuedRemoveByPublicKey.clear();
	}

	BasicAccountStateCacheDelta::HighValueAddressesTuple BasicAccountStateCacheDelta::highValueAddresses() const {
		HighValueAccountsUpdater updater(m_options, m_highValueAccounts.addresses());
		updater.update(m_pStateByAddress->deltas());
		return { updater.addresses(), updater.removedAddresses() };
	}

	void BasicAccountStateCacheDelta::updateHighValueAccounts(Height /*height*/) {

	}

	HighValueAccounts BasicAccountStateCacheDelta::detachHighValueAccounts() {
		return m_highValueAccountsUpdater.detachView();
	}
}}
