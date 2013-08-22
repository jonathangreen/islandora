<?php

/**
 * @file
 * This file contains a class to include the Tuque php library.
 */

$islandora_module_path = drupal_get_path('module', 'islandora');

set_include_path(get_include_path() . PATH_SEPARATOR . 'sites/all/libraries/tuque' . PATH_SEPARATOR . "$islandora_module_path/libraries/tuque/");

// Do this until we expose these in a module or library.
@include_once 'sites/all/libraries/tuque/RepositoryFactory.php';
@include_once "$islandora_module_path/libraries/tuque/RepositoryFactory.php";

class IslandoraTuque {

  /**
   * The cache we use to connect.
   *
   * @var SimpleCache
   */
  public $cache = NULL;

  /**
   * The repository object.
   *
   * @var FedoraRepository
   */
  public $repository = NULL;

  /**
   * Constructor.
   *
   * @param array $user
   *   A Drupal user.
   * @param string $url
   *   The url to the fedora instance.
   */
  public function __construct($user = NULL, $url = NULL) {
    if (!isset($user)) {
      global $user;
    }

    if ($user->uid == 0) {
      $user_string = 'anonymous';
      $pass_string = 'anonymous';
    }
    else {
      $user_string = $user->name;
      $pass_string = $user->pass;
    }

    if (!isset($url)) {
      $url = variable_get('islandora_base_url', 'http://localhost:8080/fedora');
    }

    if (self::exists()) {
      $this->cache = new SimpleCache();
      $this->repository = new IslandoraRepositoryDecorator(RepositoryFactory::getRepository('fedora3', new RepositoryConfig($url, $user_string, $pass_string), $this->cache));
    }
  }

  /**
   * Checks if the Connection class exists.
   *
   * @returns bool
   *   TRUE if the Connection class exists FALSE otherwise.
   */
  public static function exists() {
    return class_exists('RepositoryFactory');
  }

  /**
   * Displays and error for use when Tuque is not installed.
   */
  public static function getError() {
    $islandora_doc_link = l(t('Islandora documentation'), 'https://wiki.duraspace.org/display/ISLANDORA/Islandora');
    $tuque_link = l(t('Tuque Fedora API'), 'http://github.com/islandora/tuque');
    $message = t('Islandora requires the !tuque_url. Please install in /sites/all/libraries/tuque before continuing. See the !islandora_url.', array('!tuque_url' => $tuque_link, '!islandora_url' => $islandora_doc_link));
    drupal_set_message(filter_xss($message), 'error', FALSE);
  }
}

class IslandoraRepositoryDecorator extends RepositoryDecorator {
  public function constructObject($id = NULL, $create_uuid = FALSE) {
    return new IslandoraObjectDecorator(parent::constructObject($id, $create_uuid));
  }

  public function ingestObject(AbstractObject &$object) {
    foreach ($object as $dsid => $datastream) {
      $datastream_context = array(
        'action' => 'ingest',
        'block' => FALSE,
      );
      islandora_alter_datastream($object, $datastream, $datastream_context);
      if ($datastream_context['block']) {
        throw new Exception(t('Object ingest blocked due to ingest of @dsid being blocked.', array(
          '@dsid' => $dsid,
        )));
      }
    }

    $object_context = array(
      'action' => 'ingest',
      'block' => FALSE,
    );

    islandora_alter_object($object, $object_context);

    if ($object_context['block']) {
      throw new Exception('Ingest Object was blocked.');
    }

    $object = new IslandoraObjectDecorator(parent::ingestObject($object));
    islandora_invoke_object_hooks(ISLANDORA_OBJECT_INGESTED_HOOK, $object->models, $object);

    // Call the ingested datastream hooks for NewFedoraObject's after the
    // object had been ingested.
    foreach ($object as $dsid => $datastream) {
      islandora_invoke_datastream_hooks(ISLANDORA_DATASTREAM_INGESTED_HOOK, $object->models, $dsid, $object, $datastream);
    }

    return $object;
  }

  public function getObject($id) {
    return new IslandoraObjectDecorator(parent::getObject($id));
  }
}

class IslandoraObjectDecorator extends ObjectDecorator {
  public function constructDatastream($id, $control_group = 'M') {
    return new IslandoraDatastreamDecorator(parent::constructDatastream($id, $control_group));
  }

  public function getDatastream($id) {
    return new IslandoraDatastreamDecorator(parent::getDatastream($id));
  }

  public function ingestDatastream(&$ds) {
    if ($this->ingested) {
      $object = $ds->parent;
      $context = array(
        'action' => 'ingest',
        'block' => FALSE,
      );
      islandora_alter_datastream($object, $ds, $context);
      if ($context['block']) {
        throw new Exception('Ingest Datastream was blocked.');
      }
    }

    $ds = new IslandoraDatastreamDecorator(parent::ingestDatastream($ds));

    if ($this->ingested) {
      islandora_invoke_datastream_hooks(ISLANDORA_DATASTREAM_INGESTED_HOOK, $object->models, $datastream->id, $object, $datastream);
    }

    return $ds;
  }
}

class IslandoraDatastreamDecorator extends DatastreamDecorator {
  protected function stateMagicProperty($function, $value) {
    if ($this->ingested) {
      islandora_modify_datastream_alter($this, array('dsState' => $value));
      $ret = parent::modifyDatastream($pid, $dsid, $params);
      islandora_invoke_datastream_hooks(ISLANDORA_DATASTREAM_MODIFIED_HOOK, $object->models, $dsid, $object, $datastream);
      if (isset($params['dsState']) && $params['dsState'] == 'D') {
        islandora_invoke_datastream_hooks(ISLANDORA_DATASTREAM_PURGED_HOOK, $object->models, $dsid, $object, $dsid);
      }
      return $ret;
    }
  }
}

function islandora_modify_datastream_alter($function, AbstractDatastream $datastream, array $params) {
  $context = array(
    'action' => 'modify',
    'block' => FALSE,
    'params' => $params,
  );

  islandora_alter_datastream($datastream->parent, $datastream, $context);

  if ($context['block']) {
    throw new Exception('Modify Datastream was blocked.');
  }
}

/**
 * Allow modules to alter an object before a mutable event occurs.
 */
function islandora_alter_object(AbstractObject $object, array &$context) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  drupal_alter(islandora_build_hook_list('islandora_object', $object->models), $object, $context);
}

/**
 * Allow modules to alter a datastream before a mutable event occurs.
 */
function islandora_alter_datastream(AbstractObject $object, AbstractDatastream $datastream, array &$context) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $types = array();
  foreach ($object->models as $model) {
    $types[] = "{$model}_{$datastream->id}";
  }
  drupal_alter(islandora_build_hook_list('islandora_datastream', $types), $object, $datastream, $context);
}


/**
 * Constructs a list of hooks from the given paramenters and invokes them.
 */
function islandora_invoke_object_hooks($hook, array $models) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  return islandora_invoke_hook_list($hook, $models, array_slice(func_get_args(), 2));
}

/**
 * Constructs a list of hooks from the given paramenters and invokes them.
 */
function islandora_invoke_datastream_hooks($hook, array $models, $dsid) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $refinements = array();
  foreach ($models as $model) {
    $refinements[] = "{$model}_{$dsid}";
  }
  return islandora_invoke_hook_list($hook, $refinements, array_slice(func_get_args(), 3));
}