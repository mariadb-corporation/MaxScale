#include <maxbase/shareddata.hh>

namespace maxbase
{

CachelineAtomic<int64_t> shareddata_timestamp_generator {0};
CachelineAtomic<int64_t> num_shareddata_updater_blocks {0};
CachelineAtomic<int64_t> num_shareddata_worker_blocks {0};
}
