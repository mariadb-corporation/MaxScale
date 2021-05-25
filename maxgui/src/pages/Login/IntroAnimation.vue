<template>
    <canvas ref="canvas"></canvas>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'intro-animation',
    props: {
        bus: { type: Object, required: true },
    },
    data() {
        return {
            circles: [],
            scratch: document.createElement('canvas'),
            ctx: null,
            hasFocus: true,
        }
    },

    watch: {
        hasFocus: function(val) {
            if (val) {
                this.createCircle()
            } else {
                this.timer && clearTimeout(this.timer)
            }
        },
        isResizing: function(val) {
            if (val) this.onResize()
        },
    },

    mounted() {
        this.bus.$on('on-resize', this.onResize)
        window.onfocus = () => {
            this.hasFocus = true
        }

        window.onblur = () => {
            this.hasFocus = false
        }

        if (window.requestAnimationFrame && this.$refs.canvas) {
            this.ctx = this.$refs.canvas.getContext('2d')

            this.createCircle() // Same UX with monitoring application, copy and paste :D

            window.requestAnimationFrame(this.draw)
        }
    },

    beforeDestroy() {
        this.timer && clearTimeout(this.timer)
    },

    methods: {
        onResize() {
            let width =
                window.innerWidth && document.documentElement.clientWidth
                    ? Math.min(window.innerWidth, document.documentElement.clientWidth)
                    : window.innerWidth ||
                      document.documentElement.clientWidth ||
                      document.getElementsByTagName('body')[0].clientWidth
            let height =
                window.innerHeight && document.documentElement.clientHeight
                    ? Math.min(window.innerHeight, document.documentElement.clientHeight)
                    : window.innerHeight ||
                      document.documentElement.clientHeight ||
                      document.getElementsByTagName('body')[0].clientHeight

            this.scratch.width = this.$refs.canvas.width = width
            this.scratch.height = this.$refs.canvas.height = height
        },
        drawCircle(circle) {
            this.ctx.strokeStyle = circle.color
            this.ctx.lineWidth = 3
            this.ctx.globalAlpha = Math.max(0, circle.opacity)

            this.ctx.beginPath()
            this.ctx.arc(circle.x, circle.y, circle.radius, 0, 2 * Math.PI, true)
            this.ctx.stroke()
        },
        draw() {
            if (!this.$refs.canvas) return

            this.ctx.clearRect(0, 0, this.scratch.width, this.scratch.height)
            this.ctx.save()

            for (let i = this.circles.length - 1; i >= 0; --i) {
                this.drawCircle(this.circles[i])

                this.circles[i].radius += 3
                this.circles[i].opacity -= 0.0025
                if (this.circles[i].radius > this.$refs.canvas.width) this.circles.splice(i, 1)
            }

            this.ctx.restore()

            window.requestAnimationFrame(this.draw)
        },
        createCircle() {
            this.timer = setTimeout(() => {
                if (this.hasFocus) {
                    this.circles.unshift({
                        x: window.innerWidth - 50,
                        y: window.innerHeight - 80,
                        radius: 1,
                        opacity: 0.9,
                        color: 'white',
                    })
                    this.createCircle()
                }
            }, this.$help.range(2.5, 5) * 1000)
        },
    },
}
</script>

<style lang="scss" scoped>
canvas {
    position: absolute;
    top: 0;
    left: 0;
}
</style>
