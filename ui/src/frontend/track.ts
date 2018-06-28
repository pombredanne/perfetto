import * as m from 'mithril';
import trackShell from './track_shell';

export default {
    view () {
        return m('.track',
            { style: { border: "1px solid #ccc", padding: "20px" } }, [
            m('h1', "Track"),
            m(trackShell)
        ]);
    }
} as m.Component;