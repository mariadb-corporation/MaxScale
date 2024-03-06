<template>
    <div class="ref-points">
        <!-- TODO: Add tooltip instruction -->
        <div
            v-for="(point, i) in points"
            :key="i"
            class="ref-point cursor-crosshair absolute"
            :style="{
                width: `${pointRadius * 2}px`,
                height: `${pointRadius * 2}px`,
                borderRadius: '50%',
                backgroundColor: node.styles.highlightColor,
                top: `${point.pos.y}px`,
                left: `${point.pos.x}px`,
                zIndex: 5,
            }"
            @mousedown.stop="dragStart({ e: $event, point })"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Emits:
 * - on-drawing()
 * - on-draw-end({ node: object, cols: array })
 */
import { createShape } from '@share/components/common/MxsSvgGraphs/utils'
import { TARGET_POS } from '@share/components/common/MxsSvgGraphs/shapeConfig'

export default {
    name: 'ref-points',
    props: {
        node: { type: Object, required: true },
        entitySizeConfig: { type: Object, required: true },
        linkContainer: { type: Object, required: true },
        boardZoom: { type: Number, required: true },
        graphConfig: { type: Object, required: true },
    },
    data() {
        return {
            defDraggingStates: {
                srcAttrId: '',
                startClientPoint: null,
                startPoint: null,
                pointDirection: '',
            },
            draggingStates: null,
            path: null,
        }
    },
    computed: {
        shapeType() {
            return this.graphConfig.linkShape.type
        },
        markerWidth() {
            return this.graphConfig.marker.width
        },
        pointRadius() {
            return 5
        },
        cols() {
            return Object.values(this.node.data.defs.col_map)
        },
        points() {
            return [...this.getPoints(TARGET_POS.LEFT), ...this.getPoints(TARGET_POS.RIGHT)]
        },
    },
    created() {
        this.setDefDraggingStates()
    },
    methods: {
        /**
         * @param {string} pointDirection - either TARGET_POS.RIGHT or TARGET_POS.LEFT
         */
        getPoints(pointDirection) {
            const { headerHeight, rowHeight: k } = this.entitySizeConfig
            return this.cols.map((c, i) => ({
                pos: {
                    x:
                        (pointDirection === TARGET_POS.RIGHT ? this.node.size.width : 0) -
                        this.pointRadius,
                    y: headerHeight + (k * i + k) - k / 2 - this.pointRadius,
                },
                col: c,
                pointDirection,
            }))
        },
        setDefDraggingStates() {
            this.draggingStates = this.$helpers.lodash.cloneDeep(this.defDraggingStates)
        },
        addDragEvents() {
            document.addEventListener('mousemove', this.drawing)
            document.addEventListener('mouseup', this.drawEnd)
        },
        rmDragEvents() {
            document.removeEventListener('mousemove', this.drawing)
            document.removeEventListener('mouseup', this.drawEnd)
        },
        pathGenerator(data) {
            let targetPos = this.draggingStates.pointDirection
            if (targetPos === TARGET_POS.RIGHT && data.x1 < this.node.x)
                targetPos = TARGET_POS.INTERSECT_RIGHT
            else if (targetPos === TARGET_POS.LEFT && data.x1 > this.node.x)
                targetPos = TARGET_POS.INTERSECT_LEFT

            return createShape({
                type: this.shapeType,
                offset: this.markerWidth,
                data,
                targetPos,
            })
        },
        dragStart({ e, point }) {
            const { col, pos, pointDirection } = point
            const { width, height } = this.node.size
            const offset = width / 2

            const x0 = this.node.x + (pointDirection === TARGET_POS.LEFT ? -offset : offset)
            const y0 = this.node.y - height / 2 + pos.y + this.pointRadius

            const startPoint = { x0, y0 }

            this.draggingStates = {
                srcAttrId: col.id,
                startClientPoint: { x: e.clientX, y: e.clientY },
                startPoint,
                pointDirection,
            }

            this.path = this.linkContainer
                .append('path')
                .attr('class', 'staging-link')
                .attr('fill', 'none')
                .attr('stroke-width', '1')
                .attr('stroke', '#0e9bc0')
                .attr('stroke-dasharray', '5')
                .attr('d', this.pathGenerator({ ...startPoint, x1: x0, y1: y0 }))
            this.addDragEvents()
        },
        updatePath(diffPos) {
            const { startPoint } = this.draggingStates
            this.path.attr(
                'd',
                this.pathGenerator({
                    x0: startPoint.x0,
                    y0: startPoint.y0,
                    x1: startPoint.x0 + diffPos.x,
                    y1: startPoint.y0 + diffPos.y,
                })
            )
        },
        drawing(e) {
            document.body.classList.add('cursor--all-crosshair')
            e.stopPropagation()
            const { startClientPoint } = this.draggingStates
            const diffPos = {
                x: (e.clientX - startClientPoint.x) / this.boardZoom,
                y: (e.clientY - startClientPoint.y) / this.boardZoom,
            }
            this.updatePath(diffPos)
            this.$emit('drawing')
        },
        drawEnd() {
            document.body.classList.remove('cursor--all-crosshair')
            this.$emit('draw-end', {
                node: this.node,
                cols: [{ id: this.draggingStates.srcAttrId }],
            })
            this.path.remove()
            this.rmDragEvents()
            this.setDefDraggingStates()
        },
    },
}
</script>
