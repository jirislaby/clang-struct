// SPDX-License-Identifier: GPL-2.0-only

#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

#include <sys/mman.h>
#include <sys/stat.h>

#include <llvm/ADT/Twine.h>
#include <llvm/Support/ErrorHandling.h>

#include "Lock.h"

using namespace ClangStruct;

static const constexpr std::string shmName("clang_struct");

void LockInit::reset()
{
	m_shm = static_cast<Shm *>(MAP_FAILED);
	m_fd = -1;
}

LockInit::LockInit()
{
	reset();

	bool creator = false;
	m_fd = shm_open(shmName.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
	if (m_fd >= 0) {
		creator = true;
		ftruncate(m_fd, sizeof(struct Shm));
	} else if (errno != EEXIST || (m_fd = shm_open(shmName.c_str(), O_RDWR, 0600)) < 0)
		llvm::reportFatalUsageError(llvm::Twine("cannot open shm: ") +
					    strerror(errno));

	m_shm = static_cast<Shm *>(mmap(nullptr, sizeof(Shm), PROT_READ | PROT_WRITE,
					MAP_SHARED, m_fd, 0));
	if (m_shm == MAP_FAILED)
		llvm::reportFatalUsageError(llvm::Twine("cannot map shm: ") +
					    strerror(errno));

	if (creator) {
		pthread_mutexattr_t a;
		pthread_mutexattr_init(&a);
		pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
		pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);

		pthread_mutex_init(&m_shm->mutex, &a);

		m_shm->initialized.store(1);
		return;
	}

	while (m_shm->initialized.load() == 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

LockInit::~LockInit() {
	if (m_shm != MAP_FAILED)
		munmap(m_shm, sizeof(Shm));
	if (m_fd >= 0)
		close(m_fd);
	reset();
}

LockInit Lock::m_lockInit;
