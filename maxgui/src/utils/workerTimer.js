/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

function timeInterval(name, interval, _this) {
    name = setInterval(() => {
        _this.postMessage({ name: name, message: `${interval / 1000} Seconds arrived.` })
    }, Number(interval))
}
/**
 *Call timer method
 */
self.onmessage = function(e) {
    e.data.map(item => {
        timeInterval(item.name, item.interval, self)
    })
}
