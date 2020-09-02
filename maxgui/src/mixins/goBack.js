import { mapState } from 'vuex'

export default {
    computed: {
        ...mapState(['prev_route']),
    },
    methods: {
        goBack() {
            this.prev_route.name === 'login' || this.prev_route.name === null
                ? this.$router.push('/dashboard/servers')
                : this.$router.go(-1)
        },
    },
}
