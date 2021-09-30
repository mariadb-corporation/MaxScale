<template>
    <div>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    outlined
                    class="revert-btn text-capitalize px-2 font-weight-medium"
                    depressed
                    small
                    color="accent-dark"
                    :disabled="disableRevert"
                    v-on="on"
                    @click="$emit('on-revert')"
                >
                    {{ $t('revert') }}
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ $t('revertChanges') }}
            </span>
        </v-tooltip>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    outlined
                    class="ml-2 apply-btn text-capitalize px-2 font-weight-medium"
                    depressed
                    small
                    color="accent-dark"
                    :disabled="disableApply"
                    v-on="on"
                    @click="$emit('on-apply')"
                >
                    {{ $t('apply') }}
                </v-btn>
            </template>
            <span style="white-space: pre;" class="d-inline-block text-center">
                {{ $t('applyChanges') }}
            </span>
        </v-tooltip>
    </div>
</template>

<script>
export default {
    name: 'alter-table-toolbar',
    props: {
        disableApply: { type: Boolean, required: true },
        disableRevert: { type: Boolean, required: true },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.apply-btn {
    .v-progress-circular {
        height: 16px !important;
        width: 16px !important;
    }
}
</style>
