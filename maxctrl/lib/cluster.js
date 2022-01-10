/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();
var colors = require("colors/safe");
var flat = require("flat");

function equalResources(oldVal, newVal) {
  return (
    _.isEqual(_.get(oldVal, "attributes.parameters"), _.get(newVal, "attributes.parameters")) &&
    _.isEqual(_.get(oldVal, "relationships.servers.data"), _.get(newVal, "relationships.servers.data")) &&
    _.isEqual(_.get(oldVal, "relationships.services.data"), _.get(newVal, "relationships.services.data")) &&
    _.isEqual(_.get(oldVal, "relationships.monitors.data"), _.get(newVal, "relationships.monitors.data"))
  );
}

function sameResource(oldVal, newVal) {
  return oldVal.id == newVal.id;
}

// Return objets that are in <a> but not in <b>
function getDifference(a, b) {
  return a && b ? _.differenceWith(a.data, b.data, sameResource) : [];
}

// Removes some unwanted properties from the object
function removeUnwanted(a) {
  var relationships = [
    "relationships.services.data",
    "relationships.servers.data",
    "relationships.monitors.data",
    "relationships.filters.data",
  ];

  var res = _.pick(a, _.concat(["id", "attributes.parameters"], relationships));

  for (r of relationships) {
    var rel = _.get(res, r, []);

    for (o of rel) {
      delete o.type;
    }
  }

  return res;
}

// Return a list of objects that differ from each other
function getChangedObjects(src, dest) {
  if (!src || !dest) {
    return {};
  }

  var a = src.data;
  var b = dest.data;
  var ours = _.intersectionWith(a, b, sameResource).sort((a, b) => a.id > b.id);
  var theirs = _.intersectionWith(b, a, sameResource).sort((a, b) => a.id > b.id);

  var our_changed = _.differenceWith(ours, theirs, equalResources);
  var their_changed = _.differenceWith(theirs, ours, equalResources);

  var combined = _.zipWith(our_changed, their_changed, function (a, b) {
    var flat_ours = flat(removeUnwanted(a));
    var flat_theirs = flat(removeUnwanted(b));
    var changed_keys = _.pickBy(flat_ours, (v, k) => !_.isEqual(flat(b)[k], v));
    var res = {};

    for (k of Object.keys(changed_keys)) {
      res[k] = {
        ours: _.has(flat_ours, k) ? flat_ours[k] : null,
        theirs: _.has(flat_theirs, k) ? flat_theirs[k] : null,
      };
    }

    return [flat_ours.id, res];
  });

  return _.fromPairs(combined);
}

// Resource collections
const collections = ["servers", "monitors", "services", "users"];

// Individual resources
const endpoints = ["maxscale", "maxscale/logs", "maxscale/query_classifier"];

// Calculate a diff between two MaxScale servers
async function getDiffs(a, b) {
  var src = {};
  var dest = {};

  for (i of collections) {
    dest[i] = await simpleRequest(b, i);
    src[i] = await simpleRequest(a, i);
  }

  for (i of endpoints) {
    // Treating the resource endpoints as arrays allows the same functions to be used
    // to compare individual resources and resource collections
    dest[i] = await simpleRequest(b, i);
    dest[i].data = [dest[i].data];
    src[i] = await simpleRequest(a, i);
    src[i].data = [src[i].data];
  }

  for (i of dest.services.data) {
    dest["services/" + i.id + "/listeners"] = { data: i.attributes.listeners };
  }

  for (i of src.services.data) {
    src["services/" + i.id + "/listeners"] = { data: i.attributes.listeners };
  }

  return [src, dest];
}

// Returns a set with the parameters that can be modified at runtime.
async function getModifiableParams(host) {
  var module = await simpleRequest(host, "maxscale/modules/maxscale");
  var modifiable = _.filter(module.data.attributes.parameters, (v) => v.modifiable);
  return new Set(_.map(modifiable, (v) => v.name));
}

async function syncDiffs(host, src, dest) {
  // Delete old services
  for (i of getDifference(dest.services, src.services)) {
    // If the service has listeners, delete those first. Otherwise the deletion will fail.
    if (i.attributes.listeners) {
      for (j of i.attributes.listeners) {
        await simpleRequest(host, "services/" + i.id + "/listeners/" + j.id, { method: "DELETE" });
      }
    }

    var body = { method: "PATCH", body: _.set({}, "data.relationships", {}) };
    await simpleRequest(host, "services/" + i.id, body);
    await simpleRequest(host, "services/" + i.id, { method: "DELETE" });
  }

  // Delete old monitors
  for (i of getDifference(dest.monitors, src.monitors)) {
    var body = { method: "PATCH", body: _.set({}, "data.relationships", {}) };
    await simpleRequest(host, "monitors/" + i.id, body);
    await simpleRequest(host, "monitors/" + i.id, { method: "DELETE" });
  }

  // Delete old servers
  for (i of getDifference(dest.servers, src.servers)) {
    // The servers must be unlinked from all services and monitors before they can be deleted
    await simpleRequest(host, "servers/" + i.id, {
      method: "PATCH",
      body: _.set({}, "data.relationships", {}),
    });
    await simpleRequest(host, "servers/" + i.id, { method: "DELETE" });
  }

  // Add new servers first, this way other objects can directly define their relationships
  for (i of getDifference(src.servers, dest.servers)) {
    // Create the servers without relationships, those are generated when services and
    // monitors are handled
    var newserv = _.pick(i, ["id", "type", "attributes.parameters"]);
    await simpleRequest(host, "servers", { method: "POST", body: { data: newserv } });
  }

  // Add new monitors
  for (i of getDifference(src.monitors, dest.monitors)) {
    await simpleRequest(host, "monitors", { method: "POST", body: { data: i } });
  }

  // Add new services
  for (i of getDifference(src.services, dest.services)) {
    // We must omit the listeners as they haven't been created yet
    await simpleRequest(host, "services", {
      method: "POST",
      body: { data: _.omit(i, "relationships.listeners") },
    });

    // Create listeners for the new service right after it is created. This removes the need to update the
    // diff with the service we created that would otherwise be necessary to do if we were to use the
    // normal listener creation code.
    if (i.attributes.listeners) {
      for (j of i.attributes.listeners) {
        await simpleRequest(host, "services/" + i.id + "/listeners", { method: "POST", body: { data: j } });
      }
    }
  }

  // Add new and delete old listeners for existing services
  var all_keys = _.concat(Object.keys(src), Object.keys(dest));
  var unwanted_keys = _.concat(collections, endpoints);
  var relevant_keys = _.uniq(_.difference(all_keys, unwanted_keys));

  for (i of relevant_keys) {
    for (j of getDifference(dest[i], src[i])) {
      await simpleRequest(host, i + "/" + j.id, { method: "DELETE" });
    }
    for (j of getDifference(src[i], dest[i])) {
      await simpleRequest(host, i, { method: "POST", body: { data: j } });
    }
  }

  // PATCH all remaining resource collections in src from dest apart from the
  // user resource, as it requires passwords to be entered, and filters, that
  // cannot currently be patched.
  for (i of _.difference(collections, ["users", "filters"])) {
    for (j of src[i].data) {
      await simpleRequest(host, i + "/" + j.id, { method: "PATCH", body: { data: j } });
    }
  }

  var params = await getModifiableParams(host);

  // Do the same for individual resources
  for (i of endpoints) {
    for (j of src[i].data) {
      j.attributes.parameters = _.pickBy(j.attributes.parameters, (v, k) => params.has(k));
      await simpleRequest(host, i, { method: "PATCH", body: { data: j } });
    }
  }
}

exports.getDifference = getDifference;
exports.getChangedObjects = getChangedObjects;
exports.command = "cluster <command>";
exports.desc = "Cluster objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "diff <target>",
      "Show difference between host servers and <target>.",
      function (yargs) {
        return yargs
          .epilog(
            "The list of host servers is controlled with the --hosts option. " +
              "The target server should not be in the host list. Value of <target> " +
              "must be in HOST:PORT format"
          )
          .usage("Usage: cluster diff <target>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getDiffs(host, argv.target).then(function (diffs) {
            var output = [];
            var src = diffs[0];
            var dest = diffs[1];

            var added = {};
            var removed = {};
            var changed = {};

            _.uniq(_.concat(Object.keys(src), Object.keys(dest))).forEach(function (i) {
              added[i] = _.map(getDifference(src[i], dest[i]), (v) => v.id);
              removed[i] = _.map(getDifference(dest[i], src[i]), (v) => v.id);
              _.assign(changed, getChangedObjects(src[i], dest[i]));
            });

            // Remove empty arrays from the generated result.
            added = _.pickBy(added, (v) => v.length);
            removed = _.pickBy(removed, (v) => v.length);

            if (!_.isEmpty(added)) {
              output.push("New:");
              output.push(colors.green(JSON.stringify(added, null, 4)));
            }
            if (!_.isEmpty(removed)) {
              output.push("Deleted:");
              output.push(colors.red(JSON.stringify(removed, null, 4)));
            }
            if (!_.isEmpty(changed)) {
              output.push("Changed:");
              output.push(colors.yellow(JSON.stringify(changed, null, 4)));
            }

            return output.join(require("os").EOL);
          });
        });
      }
    )
    .command(
      "sync <target>",
      "Synchronize the cluster with target MaxScale server.",
      function (yargs) {
        return yargs
          .epilog(
            "This command will alter all MaxScale instances given in the --hosts " +
              "option to represent the <target> MaxScale. Value of <target> " +
              "must be in HOST:PORT format. Synchronization can be attempted again if a previous " +
              "attempt failed due to a network failure or some other ephemeral error. Any other " +
              "errors require manual synchronization of the MaxScale configuration files and a " +
              "restart of the failed Maxscale.\n\n" +
              "Note: New objects created by `cluster sync` will have a placeholder value and " +
              "must be manually updated. Passwords for existing objects will not be updated " +
              "by `cluster sync` and must also be manually updated."
          )
          .usage("Usage: cluster sync <target>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getDiffs(argv.target, host).then((diffs) => syncDiffs(host, diffs[0], diffs[1]));
        });
      }
    )
    .usage("Usage: cluster <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
