<template>
  <svg>
    <defs>
      <pattern id="grid" width="60" height="20" patternUnits="userSpaceOnUse">
        <line x1="4" x2="60" y1="20" y2="20" stroke-width="2" stroke-dasharray="4" />
        <line x1="60" x2="60" y1="0" y2="20" />
      </pattern>
    </defs>
    <rect width="100%" height="100%" fill="url(#grid)" />
  </svg>
</template>

<style lang="scss" scoped>
line {
  stroke: currentColor;
}
svg {
  width: 100%;
  height: 100%;
}
</style>
