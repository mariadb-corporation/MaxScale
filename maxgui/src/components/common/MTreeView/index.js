/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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
                hover: this.isHover,
            }
        },
    },
    methods: {
        onMouseEnter() {
            this.isHover = true
        },
        onMouseLeave() {
            this.isHover = false
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
                        click: () => {
                            if (this.openOnClick && this.hasChildren) {
                                this.checkChildren().then(this.open)
                            } else if (this.activatable && !this.disabled) {
                                this.isActive = !this.isActive
                                this.treeview.updateActive(this.key, this.isActive)
                                this.treeview.emitActive()
                            }
                        },
                        dblclick: () => {
                            this.treeview.emitDblclick(this.item)
                        },
                    },
                }),
                children
            )

            element.data = element.data || {}
            this._g(element.data, {
                mouseenter: this.onMouseEnter,
                mouseleave: this.onMouseLeave,
            })

            return element
        },
    },
})

export default VTreeview.mixin({
    methods: {
        emitDblclick(item) {
            this.$emit('item:dblclick', item)
        },
    },
})
