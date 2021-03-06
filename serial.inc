<?php

/**
 * @file
 * Internal functions for the Serial module.
 *
 * Note: This module uses php in SQL to support dynamic table names.
 * (required because each serial field needs a dedicated dynamic table).
 * However, all table names are safe (passed through db_escape_table).
 *
 * It seems that this is better than using table names as arguments, e.g.
 *   $query = 'INSERT INTO %s (nid) VALUES(%d)';
 *   db_query($query, db_prefix_tables('{'. $table .'}'), $nid);
 */

/**
 * Creates an assistant serial table for a new created field.
 *
 * @param $field
 *   a serial field
 * @param $instance
 *   a new instance of that serial field
 * @param $projects
 *   list of all projects in Germ
 */
function _serial_create_table($field, $instance, $projects) {
  foreach ($projects as $project) {
      $table = _serial_get_field_table_name($field, $instance, $project);
      $schema = _serial_get_table_schema();
      db_create_table($table, $schema);
  }
}

/**
 * Drops an assistant serial table for a deleted field.
 *
 * @param $field
 *   a serial field
 * @param $instance
 *   a deleted instance of that serial field
 * @param $projects
 *   list of projects in Germ
 */
function _serial_drop_table($field, $instance, $projects) {
  foreach($projects as $project) {
    $table = _serial_get_field_table_name($field, $instance, $project);
    db_drop_table($table);
  }
}

/**
 * Renames serial table(s) when a content type us renamed.
 *
 * @param $old_type
 *   an old node type machine name
 * @param $new_type
 *   a new node type machine name
 */
function _serial_rename_tables($old_type, $new_type) {

  // Build the query to find all affected tables.
  $query = db_select('field_config', 'f')->fields('f', array('field_name'));
  $table_joined_alias = $query->join(
      'field_config_instance', 'i',
      '(f.field_name = i.field_name) AND ' .
      '(f.type = :field_type) AND (i.bundle = :bundle_type)',
      array(':field_type' => 'serial', ':bundle_type' => $new_type)
  );

  // Add an access check and execute it.
  $result = $query->addTag('node_access')->execute();

  // Rename each affected table.
  foreach ($result as $record) {
    $old_table = _serial_get_table_name($old_type, $record->field_name);
    $new_table = _serial_get_table_name($new_type, $record->field_name);
    db_rename_table($old_table, $new_table);
  }
}

/**
 * Gets the name of the assistant table for a specific field.
 *
 * @param $field
 *   a serial field
 * @param $instance
 *   an instance of that serial field
 * @param $project
 *   project for which we need serial id
 * @return
 *   the name of the assistant table of the specified field instance.
 */
function _serial_get_field_table_name($field, $instance, $project) {
  // $field['field_name'] = 'field_test'
  // $instance['bundle'] = 'issue'
  return _serial_get_table_name($instance['bundle'], $field['field_name'], $project);
}

/**
 * Gets the name of the assistant table for a specific field.
 *
 * @param $bundle
 *   the name of the entity type that contains the field
 * @param $field_name
 *   the name of the field
 * @param $project
 *   project for which serial ids are needed
 * @return
 *   the name of the assistant table of the specified field.
 */
function _serial_get_table_name($bundle, $field_name, $project) {
  // $project_name
  return db_escape_table( // be on the safe side
    'serial_' . $bundle . '_' . $project . '_' . $field_name);
}

/**
 * Gets the schema of the assistant tables for generating serial values.
 *
 * @return
 *   the assistant table schema.
 */
function _serial_get_table_schema() {
  return array(
    'fields' => array(
      'sid' => array(
        'type' => 'serial',
        'unsigned' => TRUE,
        'not null' => TRUE,
        'description' => 'The atomic serial field.',
      ),
      'nid' => array(
        'type' => 'int',
        'unsigned' => TRUE,
        'not null' => TRUE,
        'description' => 'Id of the owner node.',
      ),
    ),
    'primary key' => array('sid'),
    'unique keys' => array(
      'nid' => array('nid'),
    ),
  );
}

/**
 * Generates a unique serial value (unique per node type).
 *
 * @param $nid
 *   id of the node for which to generate a serial value
 * @param $bundle
 *   a containing bundle (e.g. content type)
 * @param $field_name
 *   the field name
 * @param $project
 *   project name for which to get the serial value
 * @param $delete
 *   indicates if temporary records should be deleted
 * @return
 *   the unique serial value number.
 */
function _serial_generate_value($nid, $bundle, $field_name, $project, $delete = TRUE) {
  // $project_name
  // Get the name of the relevant table.
  $table = _serial_get_table_name($bundle, $field_name, $project);

  // Create a temporary record for this node and retrieve the serial value.
  $sid = db_insert($table)
    ->fields(array(
      'nid' => $nid,
    ))
    ->execute();

  // If there's a reason why it's come back undefined, reset it.
  $sid = isset($sid) ? $sid : 0;

  // Delete old temporary records:
  if ($delete && ($sid % 10) == 0) {
    db_delete($table)
      ->condition('nid', $nid, '<')
      ->execute();
  }

  // Return the new unique serial value:
  return $sid;
}

/**
 * Initializes the value of a new serial field in existing nodes.
 *
 * @param $bundle
 *   a containing bundle (e.g. content type)
 * @param $field_name
 *   the field name
 * @param $projects
 *   list of all projects in Germ
 * @return
 *   the number of existing nodes that have been initialized.
 */
function _serial_init_old_nodes($bundle, $field_name, $projects) {
  // Retrieve all the node ids of that type:
  $query = "SELECT nid FROM {node} WHERE type = :type ORDER BY nid";
    // TODO: Currently works only for nodes - should support comments and users.
  $result = db_query($query, array('type' => $bundle));
  $count = array();
  // Allocate a serial number for every old node:
  foreach($projects as $project) {
      $count[$project] = 0;
      foreach ($result as $node) {
        $nid = $node->nid;
        $node = node_load($nid);
        $sid = _serial_generate_value($nid, $bundle, $field_name, $project, FALSE);
        $node->{$field_name} = array('und' => array(array('value' => $sid)));
        node_save($node);
        $last_nid = $nid;
        $count[$project]++;
      }

      // Delete temporary records (except the last):
      if (isset($last_nid)) {
        $serial_table = _serial_get_table_name($bundle, $field_name, $project);
        db_delete($serial_table)
          ->condition('nid', $last_nid, '<')
          ->execute();
      }
  }
  // Return the number of existing nodes that have been initialized:
  return $count;
}

function _get_germ_projects() {
    $projects = array();
    $query = new EntityFieldQuery;
    $result = $query
    ->entityCondition('entity_type', 'node')
    ->propertyCondition('type', 'project')
    ->propertyCondition('status', 1)
    ->execute();
    if (!empty($result['node'])) {
        $nodes = node_load_multiple(array_keys($result['node']));
        foreach ($nodes as $project) {
            $projects[] = $project->title;
        }        
    }
    return $projects;
}
