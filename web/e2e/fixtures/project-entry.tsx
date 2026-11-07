// A project that reuses the reference interface and changes no reference file: it labels its own status
// item, changes some text and gives its device a longer restart limit.
import '../../reference/src/style.css';
import { mountReferenceInterface } from '../../reference/src/app.tsx';
import { configScreen } from '../../reference/src/screens/config.tsx';
import { homeScreen } from '../../reference/src/screens/home.tsx';
import { networkScreen } from '../../reference/src/screens/network.tsx';
import { rebootScreen } from '../../reference/src/screens/reboot.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountReferenceInterface(root, {
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
