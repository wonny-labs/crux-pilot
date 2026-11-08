// A project that reuses the Preact interface and changes no Preact file: it labels its own status
// item, changes some text and gives its device a longer restart limit.
import '../../../samples/web/preact/src/style.css';
import { mountPreactInterface } from '../../../samples/web/preact/src/app.tsx';
import { configScreen } from '../../../samples/web/preact/src/screens/config.tsx';
import { homeScreen } from '../../../samples/web/preact/src/screens/home.tsx';
import { networkScreen } from '../../../samples/web/preact/src/screens/network.tsx';
import { rebootScreen } from '../../../samples/web/preact/src/screens/reboot.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountPreactInterface(root, {
    screens: [homeScreen, configScreen, networkScreen, rebootScreen],
    homeItems: [
      {
        name: 'app.cycle_count',
        label: 'Machine cycles',
        section: 'Machine',
        format: (value) => `${String(value)} cycles`,
      },
    ],
    text: { product: 'Example Press', signIn: { title: 'Operator sign-in', submit: 'Sign in' } },
    client: { timing: { rebootLimitMs: 600_000 } },
  });
}
