#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <cassert>

#include "contrib/pa_ringbuffer.h"
#include "errors.hpp"

template <typename T1, typename T2>
class RingBuffer {
public:
	virtual T2 WriteCapacity() const = 0;
	virtual T2 ReadCapacity() const = 0;
	virtual T2 Write(T1 *start, T2 count) = 0;
	virtual T2 Read(T1 *start, T2 count) = 0;
	virtual void Flush() = 0;
};

template <typename T1, typename T2, int P>
class PaRingBuffer : public RingBuffer<T1, T2> {
public:
	PaRingBuffer(int size = sizeof(T1))
	{
		this->rb = new PaUtilRingBuffer;
		this->buffer = new char[(1 << P) * size];

		if (PaUtil_InitializeRingBuffer(
		                    this->rb, size,
		                    static_cast<ring_buffer_size_t>(1 << P),
		                    this->buffer) != 0) {
			throw Error(ErrorCode::INTERNAL_ERROR,
			            "ringbuf failed to init");
		}
	}

	~PaRingBuffer()
	{
		assert(this->rb != nullptr);
		delete this->rb;

		assert(this->buffer != nullptr);
		delete[] this->buffer;
	}

	T2 WriteCapacity() const
	{
		return static_cast<T2>(
		                PaUtil_GetRingBufferWriteAvailable(this->rb));
	}

	T2 ReadCapacity() const
	{
		return static_cast<T2>(
		                PaUtil_GetRingBufferReadAvailable(this->rb));
	}

	T2 Write(T1 *start, T2 count)
	{
		return static_cast<T2>(PaUtil_WriteRingBuffer(
		                this->rb, start,
		                static_cast<ring_buffer_size_t>(count)));
	}

	T2 Read(T1 *start, T2 count)
	{
		return static_cast<T2>(PaUtil_ReadRingBuffer(
		                this->rb, start,
		                static_cast<ring_buffer_size_t>(count)));
	}

	void Flush()
	{
		PaUtil_FlushRingBuffer(this->rb);
	}

private:
	char *buffer;
	PaUtilRingBuffer *rb;
};

#endif // RINGBUFFER_H
