/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export async function inputChangeMock({ wrapper, value, selector = '' }) {
  if (selector) await wrapper.find(selector).get('input').setValue(value)
  else await wrapper.find('input').setValue(value)
}

/**
 * @param {Object} inputComponent - mounted input component
 * @returns {Object} - returns v-messages__message element
 */
export function getErrMsgEle(inputComponent) {
  return inputComponent.find('.v-messages__message')
}
