import { getCurrentInstance } from 'vue'

export function useTypy() {
  const vm = getCurrentInstance()
  return vm.appContext.config.globalProperties.$typy
}

export function useLogger() {
  const vm = getCurrentInstance()
  return vm.appContext.config.globalProperties.$logger
}
