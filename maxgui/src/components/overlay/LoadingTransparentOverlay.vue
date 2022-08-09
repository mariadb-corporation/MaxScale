<template>
    <v-overlay z-index="9999" class="loading-transparent-overlay">
        <v-progress-circular indeterminate size="64" />
    </v-overlay>
</template>
<style lang="scss">
.loading-transparent-overlay .v-overlay__content {
    display: flex;
    justify-content: center;
    align-items: center;
}
</style>
