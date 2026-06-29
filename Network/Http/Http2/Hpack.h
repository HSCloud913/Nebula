//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Type.h"
#include "Http/HttpCommon.h"

BEGIN_NS(ne::network::http_2)

	// RFC 7541 HPACK — minimal implementation.
	// Encode : literal representation only (no Huffman output, no dynamic table).
	// Decode : static table + literal fields; Huffman decode supported for ASCII (0-127).
	namespace Hpack
	{
		[[nodiscard]] std::vector<ne::byte_t> Encode(
			ne::string_view_t                   _method,
			ne::string_view_t                   _path,
			ne::string_view_t                   _scheme,
			ne::string_view_t                   _authority,
			const ne::network::HttpHeaders&     _headers
		);

		// Returns headers including pseudo-headers (:status, :path …)
		[[nodiscard]] ne::network::HttpHeaders Decode(
			const ne::byte_t* _data,
			std::size_t       _len
		);
	}

END_NS
