/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// https://github.com/vuetifyjs/vuetify/issues/8416
import { VTreeview, VTreeviewNode } from 'vuetify/lib'

VTreeviewNode.mixin({
    data: () => ({
        isHover: false,
    }),
    computed: {
        scopedProps() {
            return {
                item: this.item,
                leaf: !this.children,
                selected: this.isSelected,
                indeterminate: this.isIndeterminate,
                active: this.isActive,
                open: this.isOpen,
                isHover: this.isHover,
            }
        },
    },
    methods: {
        onMouseEnter(item) {
            this.isHover = true
            this.treeview.emitItemHovered(item)
        },
        onMouseLeave() {
            this.isHover = false
            this.treeview.emitItemHovered(null)
        },
        genNode() {
            const children = [this.genContent()]

            if (this.selectable) children.unshift(this.genCheckbox())

            if (this.hasChildren) {
                children.unshift(this.genToggle())
            } else {
                children.unshift(...this.genLevel(1))
            }

            children.unshift(...this.genLevel(this.level))

            const element = this.$createElement(
                'div',
                this.setTextColor(this.isActive && this.color, {
                    staticClass: 'v-treeview-node__root',
                    class: {
                        [this.activeClass]: this.isActive,
                    },
                    on: {
                        click: e => {
                            e.preventDefault()
                            e.stopPropagation()
                            if (this.openOnClick && this.hasChildren) {
                                this.checkChildren().then(this.open)
                            } else if (this.activatable && !this.disabled) {
                                this.isActive = !this.isActive
                                this.treeview.updateActive(this.key, this.isActive)
                                this.treeview.emitActive()
                            }
                            // canBeHighlighted
                            if (this.item.canBeHighlighted) {
                                //persist highlighting active node when toggle node
                                this.isActive = true
                                this.treeview.updateActive(this.key, this.isActive)
                                this.treeview.emitActive()
                            }
                            this.treeview.emitClick(this.item)
                        },
                        dblclick: () => {
                            this.treeview.emitDblclick(this.item)
                        },
                        contextmenu: e => {
                            e.preventDefault()
                            e.stopPropagation()
                            this.treeview.emitCtxClick({ e, item: this.item })
                        },
                    },
                }),
                children
            )

            element.data = element.data || {}
            this._g(element.data, {
                mouseenter: () => this.onMouseEnter(this.item),
                mouseleave: () => this.onMouseLeave(this.item),
            })

            return element
        },
    },
})

export default VTreeview.mixin({
    methods: {
        emitClick(item) {
            this.$emit('item:click', item)
        },
        emitDblclick(item) {
            this.$emit('item:dblclick', item)
        },
        emitCtxClick({ e, item }) {
            this.$emit('item:contextmenu', { e, item })
        },
        emitItemHovered(item) {
            this.$emit('item:hovered', item)
        },
    },
})
