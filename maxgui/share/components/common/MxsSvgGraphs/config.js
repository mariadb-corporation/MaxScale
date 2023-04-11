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

export default () => ({
    link: {
        containerClass: 'link_container',
        pathClass: 'link_path',
        invisiblePathClass: 'link_path__invisible',
        /**
         * Path attributes can also be a function
         * e.g. element.attr('stroke', color:(d) => d.linkStyles.color )
         */
        color: '#0e9bc0',
        strokeWidth: 2.5,
        invisibleStrokeWidth: 12,
        dashArr: '5',
        opacity: 0.5,
        hover: {
            strokeWidth: 2.5,
            dashArr: '0',
            opacity: 1,
        },
        dragging: {
            strokeWidth: 2.5,
            dashArr: '0',
            opacity: 1,
        },
    },
    linkShape: {
        type: LINK_SHAPES.ORTHO,
        entitySizeConfig: {
            rowHeight: 32,
            // Reserve 4 px to make sure point won't be at the top or bottom edge of the row
            rowOffset: 4,
            // Ensure that the marker remains visible while dragging a node by allocating a specific width.
            markerWidth: 15,
        },
    },
})
