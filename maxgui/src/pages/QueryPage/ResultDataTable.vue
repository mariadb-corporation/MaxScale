<template>
    <div>
        <!-- TODO: add filter input (recalculate height of table after adding it)
            Add resizable column feat
         -->
        <data-table
            tableClass="data-table-full--max-width-columns"
            :search="search_keyword"
            :headers="headers"
            :data="rows"
            showAll
            dense
            fixed-header
            :height="height"
        />
    </div>
</template>

<script>
export default {
    name: 'result-data-table',
    props: {
        headers: { type: Array, require: true },
        rows: { type: Array, require: true },
        height: { type: String, require: true },
    },
    data() {
        return {
            search_keyword: '',
        }
    },
}
</script>
