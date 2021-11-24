<template>
    <code v-if="source" class="d-block mariadb-code-style">
        <span class="pr-4 color text-field-text">{{ source.timestamp }}</span>
        <span
            class="log-level d-inline-flex justify-start"
            :class="logPriorityColorClasses(source.priority)"
        >
            <span>
                <icon-sprite-sheet size="13" :frame="source.priority">
                    logPriorities
                </icon-sprite-sheet>
            </span>
            <span class="ml-1">{{ source.priority }}</span>
        </span>

        <span class="log-level-divider">:</span>

        <span class="text-wrap" :class="logPriorityColorClasses(source.priority)">
            {{ source.message }}
        </span>
    </code>
</template>

<script>
export default {
    name: 'log-line',
    props: {
        source: {
            type: Object,
            default() {
                return {
                    id: '',
                    timestamp: '',
                    priority: '',
                    message: '',
                }
            },
        },
    },
    methods: {
        logPriorityColorClasses: level =>
            `color text-${level} ${level === 'alert' ? 'font-weight-bold' : ''}`,
    },
}
</script>
<style lang="scss" scoped>
.log-level {
    width: 75px;
}
.log-lines-wrapper-enter-active,
.log-lines-wrapper-leave-active {
    height: 100%;
    transition: 0.3s all ease;
}

.log-lines-wrapper-enter,
.log-lines-wrapper-leave-active {
    opacity: 0;
    transform: translateY(-20px);
}
</style>
