globalThis.scenarioEvents = ['collector'];
history.pushState({sample: 1}, '', '/next#offline');
navigator.userAgent;
navigator.scenarioMissingApi;
queueMicrotask(() => scenarioEvents.push('microtask'));
setTimeout(() => {
  scenarioEvents.push('timer');
  location.assign('/navigation-intent');
}, 5);
