// See pak_assets.hpp.
#include "pak_assets.hpp"

extern "C" {
#include "hal.h"
#include "pakfs.h"
}

namespace rvstack {

int assets_mount()
{
	if (pakfs_mount() == 0)
		return 0;
	// No pak picked (or not a pakfs): auto-bind by name, then re-pull.
	int r = pak_bind_named("skyroads.pak");
	if (r != 0)
		return r;
	return pakfs_mount();
}

skyroads::data::Bytes asset_bytes(const char* name)
{
	uint32_t size = 0;
	const void* p = pakfs_data(name, &size);
	if (p == nullptr) {
		// 0xDEADPAK0 + directory index would be nicer, but the name is
		// what you need: beacon the first two chars and hang visibly.
		sys_diag(0xDEAD9A00u | (uint32_t)(unsigned char)name[0]);
		for (;;)
			;
	}
	const uint8_t* bytes = static_cast<const uint8_t*>(p);
	return skyroads::data::Bytes(bytes, bytes + size);
}

} // namespace rvstack
