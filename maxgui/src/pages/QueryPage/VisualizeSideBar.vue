<template>
    <div class="pa-4">
        <h4>Visualization</h4>
        <v-select
            v-model="selectedGraph"
            :items="graphs"
            outlined
            class="mt-4 std mariadb-select-input error--text__bottom"
            :menu-props="{
                contentClass: 'mariadb-select-v-menu',
                bottom: true,
                offsetY: true,
            }"
            dense
            :height="36"
            hide-details="auto"
        />
        <!-- TODO: add graph configuration -->
    </div>
</template>

<script>
export default {
    name: 'visualize-sidebar',

    data() {
        return {
            selectedGraph: 'No Visualization',
            graphs: ['No Visualization', 'Line', 'Bar - Horizontal', 'Bar - Vertical'],
        }
    },
}
</script>

<style lang="scss" scoped></style>
