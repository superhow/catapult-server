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
#include "catapult/types.h"

namespace catapult { namespace model { class ResolverContext; } }

namespace catapult { namespace model {

	/// Allows unified handling of resolved and unresolved types.
	/// \note Single parameters are not explicit to allow this class to be a stand-in for those types.
	template<typename TUnresolved, typename TResolved>
	class Resolvable {
	private:
		enum class Type { Unresolved, Resolved };

	public:
		/// Creates a default resolvable.
		Resolvable();

		/// Creates a resolvable around \a resolved value.
		Resolvable(const TResolved& resolved);

		/// Creates a resolvable around \a unresolved value.
		Resolvable(const TUnresolved& unresolved);

	public:
		/// Returns \c true if underlying value is resolved.
		bool isResolved() const;

		/// Gets an unresolved representation of underlying value.
		TUnresolved unresolved() const;

		/// Gets a resolved representation of underlying value using \a resolvers.
		TResolved resolved(const ResolverContext& resolvers) const;

	private:
		TUnresolved m_unresolved;
		TResolved m_resolved;
		Type m_type;
	};

	/// Resolvable address.
	using ResolvableAddress = Resolvable<UnresolvedAddress, Address>;
	extern template class Resolvable<UnresolvedAddress, Address>;

	/// Resolvable mosaic id.
	using MosaicResolutionStatement = Resolvable<UnresolvedMosaicId, MosaicId>;
	extern template class Resolvable<UnresolvedMosaicId, MosaicId>;
}}
