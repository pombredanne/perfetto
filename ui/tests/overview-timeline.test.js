require('../overview-timeline');

test('expect the html element overview-timeline to be defined', () => {
  expect(customElements.get('overview-timeline')).toBeDefined();
});

test('expect overview-timeline to have a test function', () => {
  const OverviewTimeline = customElements.get('overview-timeline');

  const el = new OverviewTimeline();
  expect(el.test).toBeDefined();
});