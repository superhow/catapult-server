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
#include "NetworkInfo.h"
#include "ResolverContext.h"
#include "catapult/types.h"

namespace catapult { namespace model {

	/// Contextual information associated with a notification.
	/// \note This is passed to both stateful validators and observers.
	struct NotificationContext {
	public:
		/// Creates a notification context around \a height, \a network and \a resolvers.
		NotificationContext(catapult::Height height, const model::NetworkInfo& network, const model::ResolverContext& resolvers)
				: Height(height)
				, Network(network)
				, Resolvers(resolvers)
		{}

	public:
		/// Current height.
		const catapult::Height Height;

		/// Network info.
		const model::NetworkInfo Network;

		/// Alias resolvers.
		const model::ResolverContext Resolvers;
	};
}}
