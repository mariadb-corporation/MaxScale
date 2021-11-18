<template>
    <v-menu
        :value="activator ? isOpened : menuOpen"
        :activator="activator"
        :transition="transition"
        :left="left"
        :nudge-right="nudgeRight"
        :nudge-bottom="nudgeBottom"
        :offset-x="isOffsetX"
        :offset-y="isOffsetY"
        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
        :close-on-content-click="false"
        :open-on-hover="openOnHover"
        :open-delay="openDelay"
        min-width="auto"
        @input="$emit('input', $event)"
    >
        <template v-if="!activator" v-slot:activator="{ on }">
            <v-list-item
                v-if="isSubMenu"
                class="d-flex justify-space-between color text-text"
                v-on="on"
            >
                <v-list-item-title class="color text-text" v-text="text" />
                <v-icon size="24">
                    arrow_right
                </v-icon>
            </v-list-item>
            <div v-else v-on="on" @click="activator ? (isOpened = true) : (menuOpen = true)">
                <slot name="activator">
                    <v-list-item link dense>
                        <v-list-item-title class="color text-text" v-text="text" />
                    </v-list-item>
                </slot>
            </div>
        </template>
        <v-list>
            <template v-for="(item, index) in items">
                <v-divider v-if="item.divider" :key="index" />
                <sub-menu
                    v-else-if="item.children"
                    :key="`sub-menu-${index}`"
                    :isOffsetX="true"
                    :isOffsetY="false"
                    :isSubMenu="true"
                    :items="item.children"
                    :text="item.text"
                    :transition="nestedMenuTransition"
                    :open-delay="nestedMenuOpenDelay"
                    :open-on-hover="true"
                    @item-click="emitClickEvent"
                />
                <v-list-item
                    v-else
                    :key="`v-list-item-${index}`"
                    dense
                    link
                    @click="emitClickEvent(item)"
                >
                    <v-list-item-title class="color text-text" v-text="item.text" />
                </v-list-item>
            </template>
        </v-list>
    </v-menu>
</template>

<script>
export default {
    props: {
        // if using activator, value is used
        value: { type: Boolean, default: false },
        activator: { type: String, default: '' },
        text: { type: String, default: '' },
        items: { type: Array, required: true },
        left: { type: Boolean, default: false },
        nudgeRight: { type: Number, default: 0 },
        nudgeBottom: { type: Number, default: 0 },
        isOffsetX: { type: Boolean, default: false },
        isOffsetY: { type: Boolean, default: true },
        isSubMenu: { type: Boolean, default: false },
        openOnHover: { type: Boolean, default: false },
        transition: { type: String, default: 'slide-y-transition' },
        nestedMenuTransition: { type: String, default: 'scale-transition' },
        openDelay: { type: Number, default: 0 },
        nestedMenuOpenDelay: { type: Number, default: 150 },
    },
    data() {
        return {
            menuOpen: false,
        }
    },
    computed: {
        // use this to control menu visibility when using activator
        isOpened: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
    methods: {
        emitClickEvent(item) {
            this.$emit('item-click', item)
            if (this.activator) this.isOpened = false
            else this.menuOpen = false
        },
    },
}
</script>
