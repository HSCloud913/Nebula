#pragma once
#include "Type.h"

BEGIN_NS(ne::crypto)
	class MD5 final
	{
	public:
		explicit MD5() = default;
		~MD5() = default;

	private:
		enum
		{
			MD5BlockSize  = 512 / 8,
			MD5HashBytes  = 16,
			MD5HashValues = MD5HashBytes / 4
		};

	private:
		uint_t md5Value[MD5HashValues];

	private:
		byte_t buffer[MD5BlockSize];
		size_t bufferSize = 0;
		ulong_t length = 0;

	public:
		void Init();
		void AddBuffer(const void_t* _data, size_t _dataLength);
		[[nodiscard]] string_t Get();

	private:
		void ProcessBuffer();
		void ProcessBlock(const void_t* _data);
	};

END_NS
