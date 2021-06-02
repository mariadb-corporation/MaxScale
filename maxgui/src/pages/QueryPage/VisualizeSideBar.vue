<template>
    <div class="pa-4">
        <h5 class="mb-4">Visualization</h5>
        <label class="field__label color text-small-text"> Graph</label>
        <v-select
            v-model="selectedGraph"
            :items="graphs"
            outlined
            class="std mariadb-select-input error--text__bottom"
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

<style lang="scss" scoped>
$label-size: 0.75rem;
.field__label {
    font-size: $label-size;
}
</style>
