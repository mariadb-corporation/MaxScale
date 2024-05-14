/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
/**
 *  To use this mixin, a mousedown event needs to be listened on drag target element and do the followings assign
 *  assign true to `isDragging` and event.target to `dragTarget`
 *  @mousedown
 */
/**
 * This copies inherit styles from srcNode to dstNode
 * @param {Object} payload.srcNode - html node to be copied
 * @param {Object} payload.dstNode - target html node to pasted
 */
function copyNodeStyle({ srcNode, dstNode }) {
    const computedStyle = window.getComputedStyle(srcNode)
    Array.from(computedStyle).forEach(key =>
        dstNode.style.setProperty(
            key,
            computedStyle.getPropertyValue(key),
            computedStyle.getPropertyPriority(key)
        )
    )
}
function removeTargetDragEle(dragTargetId) {
    let elem = document.getElementById(dragTargetId)
    if (elem) elem.parentNode.removeChild(elem)
}
function addDragTargetEle({ e, dragTarget, dragTargetId }) {
    let cloneNode = dragTarget.cloneNode(true)
    cloneNode.setAttribute('id', dragTargetId)
    cloneNode.textContent = dragTarget.textContent
    copyNodeStyle({ srcNode: dragTarget, dstNode: cloneNode })
    cloneNode.style.position = 'absolute'
    cloneNode.style.top = e.clientY + 'px'
    cloneNode.style.left = e.clientX + 'px'
    cloneNode.style.zIndex = 9999
    document.getElementById('app').appendChild(cloneNode)
}
export default {
    data() {
        return {
            isDragging: false,
            dragTarget: null,
            dragTargetId: 'target-drag',
        }
    },
    watch: {
        isDragging(v) {
            if (v) this.addDragEvts()
            else this.removeDragEvts()
        },
    },
    beforeDestroy() {
        this.removeDragEvts()
    },
    methods: {
        addDragEvts() {
            document.addEventListener('mousemove', this.onDragging)
            document.addEventListener('mouseup', this.onDragEnd)
        },
        removeDragEvts() {
            document.removeEventListener('mousemove', this.onDragging)
            document.removeEventListener('mouseup', this.onDragEnd)
        },
        onDragging(e) {
            e.preventDefault()
            if (this.isDragging) {
                removeTargetDragEle(this.dragTargetId)
                addDragTargetEle({
                    e,
                    dragTarget: this.dragTarget,
                    dragTargetId: this.dragTargetId,
                })
                this.$emit('on-dragging', e)
            }
        },
        onDragEnd(e) {
            e.preventDefault()
            if (this.isDragging) {
                removeTargetDragEle(this.dragTargetId)
                this.$emit('on-dragend', e)
                this.isDragging = false
            }
        },
    },
}
