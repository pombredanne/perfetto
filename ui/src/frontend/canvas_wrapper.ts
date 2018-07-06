/*
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import * as m from 'mithril';

type CanvasWrapper = m.Comp<{
  scrollOffset: number,
  canvasElement: HTMLCanvasElement,
}, {}>;

export const canvasWrapper = {
  view({attrs}) {
    return m('.canvasWrapper', {
      style: {
        position: 'absolute',
        top: attrs.scrollOffset.toString() + 'px',
        overflow: 'none',
      }
    });
  },

  oncreate(vnode) {
    vnode.dom.appendChild(vnode.attrs.canvasElement);
  }
} as CanvasWrapper;
