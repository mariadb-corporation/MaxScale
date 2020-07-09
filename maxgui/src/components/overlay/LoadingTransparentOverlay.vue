<template>
    <v-overlay z-index="9999">
        <v-progress-circular indeterminate size="64" />
    </v-overlay>
</template>
<style lang="scss" scoped>
::v-deep .v-overlay__content {
    display: flex;
    justify-content: center;
    align-items: center;
}
</style>
