<template>
    <div>
        <portal to="page-header">
            <div class="d-flex align-center">
                <v-btn class="ml-n4" icon @click="goBack">
                    <v-icon
                        class="mr-1"
                        style="transform:rotate(90deg)"
                        size="28"
                        color="deep-ocean"
                    >
                        $vuetify.icons.arrowDown
                    </v-icon>
                </v-btn>
                <div class="d-inline-flex align-center">
                    <truncate-string :text="$route.params.id" :maxWidth="600">
                        <span
                            style="line-height: normal;"
                            class="ml-1 mb-0 color text-navigation text-h4 page-title"
                        >
                            {{ $route.params.id }}
                        </span>
                    </truncate-string>

                    <v-menu
                        v-if="$slots['setting-menu-list-item'] || $slots['setting-menu']"
                        transition="slide-y-transition"
                        offset-y
                        content-class="setting-menu"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn class="ml-2 gear-btn" icon v-on="on">
                                <v-icon size="18" color="primary">
                                    $vuetify.icons.settings
                                </v-icon>
                            </v-btn>
                        </template>
                        <v-list
                            v-if="$slots['setting-menu-list-item']"
                            class="color bg-color-background"
                        >
                            <slot name="setting-menu-list-item"></slot>
                        </v-list>

                        <div
                            v-if="$slots['setting-menu']"
                            class="color bg-color-background d-inline-flex icon-wrapper-list"
                        >
                            <slot name="setting-menu"></slot>
                        </div>
                    </v-menu>
                </div>
            </div>
        </portal>
        <portal v-if="showSearch" to="page-search">
            <global-search />
        </portal>
        <portal v-if="showCreateRscBtn" to="create-resource">
            <create-resource />
        </portal>
        <slot name="append"></slot>
    </div>
</template>
<script>
import { mapState } from 'vuex'
import goBack from 'mixins/goBack'
export default {
    name: 'details-page-title',
    mixins: [goBack],
    props: {
        showSearch: { type: Boolean, default: true },
        showCreateRscBtn: { type: Boolean, default: true },
    },
    computed: {
        ...mapState(['prev_route']),
    },
}
</script>
<style lang="scss" scoped>
.setting-menu {
    border-radius: 4px;
    border: 1px solid $field-text;
    box-shadow: none;
    margin-top: 4px;
    .icon-wrapper-list {
        min-height: 36px;
        border-radius: 4px;
    }
    .v-list {
        padding-top: 0;
        padding-bottom: 0;
    }
}
</style>
