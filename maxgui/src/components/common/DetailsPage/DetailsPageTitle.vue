<template>
    <div>
        <portal to="page-header">
            <div class="d-flex align-center">
                <v-btn class="ml-n4" icon @click="$router.go(-1)">
                    <v-icon class="mr-1" style="transform:rotate(90deg)" size="28" color="#013646">
                        $vuetify.icons.arrowDown
                    </v-icon>
                </v-btn>
                <div class="d-inline-flex align-center">
                    <h4
                        style="line-height: normal;"
                        class="ml-1 mb-0 color text-navigation display-1  page-title"
                    >
                        {{ $route.params.id }}
                    </h4>
                    <v-menu transition="slide-y-transition" offset-y content-class="setting-menu">
                        <template v-slot:activator="{ on }">
                            <v-btn class="ml-2 " icon v-on="on">
                                <v-icon size="18" color="primary">
                                    $vuetify.icons.settings
                                </v-icon>
                            </v-btn>
                        </template>

                        <div class="color bg-background d-inline-flex icon-wrapper-list">
                            <slot name="setting-menu"></slot>
                        </div>
                    </v-menu>
                </div>
            </div>
        </portal>
        <portal to="page-search">
            <global-search />
        </portal>
        <portal to="create-resource">
            <create-resource />
        </portal>
        <slot name="append"></slot>
    </div>
</template>

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
        border-radius: 0px;
    }
}
</style>
