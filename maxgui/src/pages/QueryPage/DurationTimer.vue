<template>
    <v-tooltip
        :disabled="isGettingEndTime"
        top
        transition="slide-y-transition"
        content-class="shadow-drop pa-3"
    >
        <template v-slot:activator="{ on }">
            <pre v-on="on"> {{ totalDuration }} seconds </pre>
        </template>
        <v-sheet min-width="220" max-width="450" class="color text-small-text">
            <div class="d-flex align-center color text-navigation font-weight-bold">
                <span>Total Duration:</span>
                <v-spacer />
                <span>{{ totalDuration }} seconds</span>
            </div>
            <div class="d-flex align-center">
                <span>Network delay</span>
                <v-spacer />
                <span class="color text-navigation">
                    {{ (totalDuration - executionTime).toFixed(3) }} seconds
                </span>
            </div>
            <div class="d-flex align-center">
                <span>Execution Time:</span>
                <v-spacer />
                <span class="color text-navigation"> {{ executionTime }} seconds</span>
            </div>
        </v-sheet>
    </v-tooltip>
</template>

<script>
export default {
    name: 'duration-timer',
    props: {
        executionTime: {
            type: Number, // in seconds
            required: true,
        },
        startTime: {
            type: Number, // in ms
            required: true,
        },
    },
    data() {
        return {
            totalDuration: 0,
        }
    },
    computed: {
        isGettingEndTime() {
            return this.executionTime === -1
        },
    },
    watch: {
        executionTime(v) {
            if (v === -1) this.updateSecond()
        },
    },
    mounted() {
        this.updateSecond()
    },
    methods: {
        updateSecond() {
            const now = new Date().valueOf()
            const currSec = ((now - this.startTime) / 1000).toFixed(3)
            if (this.isGettingEndTime) {
                this.totalDuration = parseFloat(currSec)
                requestAnimationFrame(this.updateSecond)
            }
        },
    },
}
</script>
