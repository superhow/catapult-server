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

#include "MultisigEntryMapper.h"
#include "mongo/src/mappers/MapperUtils.h"
#include "catapult/utils/Casting.h"

using namespace catapult::mongo::mappers;

namespace catapult { namespace mongo { namespace plugins {

	// region ToDbModel

	namespace {
		void StreamPublicKeys(bson_stream::document& builder, const std::string& keySetName, const utils::SortedKeySet& keys) {
			auto keyArray = builder << keySetName << bson_stream::open_array;
			for (const auto& key : keys)
				keyArray << ToBinary(key);

			keyArray << bson_stream::close_array;
		}
	}

	bsoncxx::document::value ToDbModel(const state::MultisigEntry& entry, const Address& accountAddress) {
		bson_stream::document builder;
		auto doc = builder
				<< "multisig" << bson_stream::open_document
					<< "accountPublicKey" << ToBinary(entry.key())
					<< "accountAddress" << ToBinary(accountAddress)
					<< "minApproval" << static_cast<int32_t>(entry.minApproval())
					<< "minRemoval" << static_cast<int32_t>(entry.minRemoval());

		StreamPublicKeys(builder, "cosignatoryAddresses", entry.cosignatoryAddresses());
		StreamPublicKeys(builder, "multisigAddresses", entry.multisigAddresses());

		return doc
				<< bson_stream::close_document
				<< bson_stream::finalize;
	}

	// endregion
}}}
