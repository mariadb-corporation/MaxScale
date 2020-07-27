import { expect } from 'chai'

/**
 * This function mockups the selection of an item. By default, it finds all v-select
 * components and expect to have one v-select only, if selector param is defined, it finds component
 * using selector
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} selector Using class only. e.g. '.class'
 * @param {*} item item to be selected
 */
export async function mockupSelection(wrapper, item, selector = '') {
    let vSelects
    if (selector) {
        vSelects = wrapper.findAll(selector)
    } else {
        vSelects = wrapper.findAllComponents({ name: 'v-select' })
    }

    expect(vSelects.length).to.be.equal(1)
    await vSelects.at(0).vm.selectItem(item)
}

/**
 * This function mockups the change of an input field. By default, it finds all input
 * elements and expect to have one element only, if selector param is defined, it finds element
 * using selector
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} selector valid DOM selector syntax. e.g. '.class' or '#id'
 * @param {String} newValue new string value for input field
 */
export async function mockupInputChange(wrapper, newValue, selector = '') {
    let inputs
    if (selector) {
        inputs = wrapper.findAll(selector)
    } else {
        inputs = wrapper.findAll('input')
    }
    inputs.at(0).element.value = newValue
    expect(inputs.length).to.be.equal(1)
    // manually triggering input event on v-text-field
    await inputs.at(0).trigger('input')
}

/**
 * This function mockups the action of opening a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
export async function mockupOpenDialog(wrapper) {
    await wrapper.setProps({
        value: true,
    })
    expect(wrapper.vm.computeShowDialog).to.be.true
}

/**
 * This function mockups the action of closing a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
export async function mockupCloseDialog(wrapper) {
    await wrapper.setProps({
        value: false,
    })
    expect(wrapper.vm.computeShowDialog).to.be.false
}

/**
 * This function mockups the action of closing a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} path path to navigate to
 */
export async function mockupRouteChanges(wrapper, path) {
    if (wrapper.vm.$route.path !== path) await wrapper.vm.$router.push(path)
}
