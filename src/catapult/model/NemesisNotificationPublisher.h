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

#pragma once
#include "NotificationPublisher.h"
#include "catapult/utils/ArraySet.h"

namespace catapult { namespace model { struct BlockChainConfiguration; } }

namespace catapult { namespace model {

	/// Options to customize behavior of nemesis notification publisher.
	struct NemesisNotificationPublisherOptions {
		/// Public keys of accounts to preemptively add.
		utils::KeySet SpecialAccountPublicKeys;
	};

	/// Extracts nemesis notification publisher options from \a config.
	NemesisNotificationPublisherOptions ExtractNemesisNotificationPublisherOptions(const BlockChainConfiguration& config);

	/// Creates a nemesis notification publisher around a base notification publisher (\a pPublisher)
	/// by raising additional notifications based on \a options (held by reference).
	std::unique_ptr<const NotificationPublisher> CreateNemesisNotificationPublisher(
			std::unique_ptr<const NotificationPublisher>&& pPublisher,
			const NemesisNotificationPublisherOptions& options);
}}
