<template>
    <virtual-scroll-table
        :benched="0"
        :headers="headers"
        :rows="colsOptsData.data"
        :itemHeight="30"
        :height="tableHeight"
        :boundingWidth="boundingWidth"
        showSelect
        v-on="$listeners"
    />
</template>

<script>
export default {
    name: 'alter-cols-opts',
    props: {
        value: { type: Object, required: true },
        tableHeight: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
    },
    computed: {
        colsOptsData: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        headers() {
            return this.colsOptsData.fields.map(field => ({ text: field }))
        },
    },
}
</script>

<style lang="scss" scoped></style>
