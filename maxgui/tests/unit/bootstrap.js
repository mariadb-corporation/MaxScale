// Required for Vuetify (Create div with a data-app attribute)
const app = document.createElement('div')
app.setAttribute('data-app', 'true')
document.body.appendChild(app)

global.requestAnimationFrame = () => null
global.cancelAnimationFrame = () => null

const localStorageMock = (() => {
    let store = {}

    return {
        getItem(key) {
            return store[key] || null
        },
        setItem(key, value) {
            store[key] = value.toString()
        },
        clear() {
            store = {}
        },
    }
})()

// global define

global.localStorage = localStorageMock
