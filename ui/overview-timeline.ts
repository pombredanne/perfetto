class OverviewTimeline extends HTMLElement {
  private static SVG_NS = 'http://www.w3.org/2000/svg';
  private static DEFAULT_WIDTH = '100%';
  private static DEFAULT_HEIGHT = '200';

  private shadow = this.attachShadow({mode: 'open'});

  constructor() {
    super();

    const width : string = this.getAttribute('width') ||
        OverviewTimeline.DEFAULT_WIDTH;
    const height : string = this.getAttribute('height') ||
        OverviewTimeline.DEFAULT_HEIGHT;

    const style = document.createElement('style');
    style.textContent = ':host { display: block; }';
    style.textContent += 'svg { background: #eee; }';

    const svgEl = document.createElementNS(OverviewTimeline.SVG_NS, 'svg');
    svgEl.setAttribute('width', width);
    svgEl.setAttribute('height', height);

    const title = document.createElementNS(OverviewTimeline.SVG_NS, 'text');
    title.textContent = 'Overview Timeline SVG';
    title.setAttribute('fill', '#339933');
    title.setAttribute('y', '110');
    title.setAttribute('x', '50');
    title.setAttribute('font-size', '64');

    svgEl.appendChild(title);

    this.shadow.appendChild(style);
    this.shadow.appendChild(svgEl);
  }

  public test()
  {

  }
}

customElements.define('overview-timeline', OverviewTimeline);
