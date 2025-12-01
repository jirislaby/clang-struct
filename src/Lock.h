// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <atomic>

#include <pthread.h>

namespace ClangStruct {

class LockInit {
private:
	struct Shm {
		pthread_mutex_t mutex;
		std::atomic_unsigned_lock_free initialized;
	};
public:
	LockInit();
	~LockInit();

	void lock() {
		if (pthread_mutex_lock(&m_shm->mutex) == EOWNERDEAD)
			pthread_mutex_consistent(&m_shm->mutex);
	}
	void unlock() { pthread_mutex_unlock(&m_shm->mutex); }

private:
	void reset();

	Shm *m_shm;
	int m_fd;
};

struct Lock {
	Lock() { m_lockInit.lock();  }
	~Lock() { m_lockInit.unlock(); }

	Lock(const Lock &) = delete;
	Lock &operator=(const Lock &) = delete;
private:
	static LockInit m_lockInit;
};

}
