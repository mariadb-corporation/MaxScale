/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export const LINK_SHAPES = {
    ORTHO: 'Orthogonal',
    ENTITY_RELATION: 'Entity Relation',
    STRAIGHT: 'Straight',
}

export const TARGET_POS = {
    RIGHT: 'right',
    LEFT: 'left',
    INTERSECT: 'intersect',
}

export const EVENT_TYPES = {
    HOVER: 'hover',
    DRAGGING: 'dragging',
    NONE: '', // for reversing the styles
}

export default () => ({
    link: {
        containerClass: 'link_container',
        pathClass: 'link_path',
        invisiblePathClass: 'link_path__invisible',
        /**
         * Path attributes can also be a function
         * e.g. element.attr('stroke', color:(d) => d.styles.color )
         */
        color: '#0e9bc0',
        strokeWidth: 2.5,
        invisibleStrokeWidth: 12,
        invisibleOpacity: 0,
        invisibleHighlightColor: '#0e9bc0',
        dashArr: '5',
        opacity: 0.5,
        [EVENT_TYPES.HOVER]: { opacity: 1 },
        [EVENT_TYPES.DRAGGING]: { opacity: 1 },
        /**
         * false: Link drawn from source node center to target node center.
         * true: Link drawn from source node attribute to target node attribute. Each
         * link must contain relationshipData.
         * e.g.  relationshipData: { source_attr: 'id', target_attr: 'id' }
         */
        isAttrToAttr: false,
    },
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

const straight = 'M 8 0 L 18 0' // straight line
const optionalSymbol = 'M 0 0 a 4 4 0 1 0 8 0 a 4 4 0 1 0 -8 0'
const manySymbol = `${straight} M 8 0 L 18 -5 M 8 0 L 18 5`

export const MIN_MAX_CARDINALITY = {
    ONE: '1',
    ONLY_ONE: '1..1',
    ZERO_OR_ONE: '0..1',
    MANY: 'N',
    ONE_OR_MANY: '1..N',
    ZERO_OR_MANY: '0..N',
}

export const CARDINALITY_SYMBOLS = {
    [MIN_MAX_CARDINALITY.ONE]: `${straight} M 13 -5 L 13 5`,
    [MIN_MAX_CARDINALITY.ONLY_ONE]: `M 3 0 L 18 0 M 8 -5 L 8 5 M 13 -5 L 13 5`,
    [MIN_MAX_CARDINALITY.ZERO_OR_ONE]: `${optionalSymbol} ${straight} M 13 -5 L 13 5`,
    [MIN_MAX_CARDINALITY.MANY]: `M 3 0 L 8 0 ${manySymbol}`,
    [MIN_MAX_CARDINALITY.ONE_OR_MANY]: `M 3 0 L 8 0 M 8 -5 L 8 5 ${manySymbol}`,
    [MIN_MAX_CARDINALITY.ZERO_OR_MANY]: `${optionalSymbol} ${manySymbol}`,
}
