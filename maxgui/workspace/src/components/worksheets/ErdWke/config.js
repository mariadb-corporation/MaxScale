/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { getLinkConfig } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'

const optionalSymbol = 'M 0 0 a 4 4 0 1 0 9 0 a 4 4 0 1 0 -9 0'
const manySymbol = 'M 9 0 L 18 0 M 9 0 L 18 -5 M 9 0 L 18 5'

export const RELATIONSHIP_OPTIONALITY = {
    MANDATORY: '1',
    OPTIONAL: '0',
}

export const MIN_MAX_CARDINALITY = {
    ONLY_ONE: '1..1',
    ZERO_OR_ONE: '0..1',
    ONE_OR_MANY: '1..N',
    ZERO_OR_MANY: '0..N',
}

export const CARDINALITY_SYMBOLS = {
    [MIN_MAX_CARDINALITY.ONLY_ONE]: `M 0 0 L 18 0 M 6 -5 L 6 5 M 11 -5 L 11 5`,
    [MIN_MAX_CARDINALITY.ZERO_OR_ONE]: `${optionalSymbol} M 9 0 L 18 0 M 13.5 -5 L 13.5 5`,
    [MIN_MAX_CARDINALITY.ONE_OR_MANY]: `M 0 0 L 9 0 M 9 -5 L 9 5 ${manySymbol}`,
    [MIN_MAX_CARDINALITY.ZERO_OR_MANY]: `${optionalSymbol} ${manySymbol}`,
}

export const getConfig = () => ({
    ...getLinkConfig(),
    marker: {
        width: 18,
        markerClass: 'entity-marker__path',
    },
    linkShape: {
        type: LINK_SHAPES.ORTHO,
        entitySizeConfig: {
            rowHeight: 32,
            // Reserve 4 px to make sure point won't be at the top or bottom edge of the row
            rowOffset: 4,
            headerHeight: 32,
        },
    },
})
