<?php

function pum_notifications_drush_command() {
  $items['pum-notifications'] = array(
    'description' => t('Send PUMnet notifications.'),
    'callback' => 'pum_notifications_send_email',
    'aliases' => array(),
  );
  return $items;
}

function pum_notifications_send_email() {
  global $user;
  $is_debug = drush_get_option('debug', false);

  $original_user = clone $user;
  drush_print('Start sending e-mails');

  $query = db_query("SELECT u.uid FROM {users} u WHERE u.status = '1' AND u.uid NOT IN (SELECT entity_id FROM field_data_field_next_notification WHERE entity_type = 'user')");
  if ($query->execute()) {
    while($uid = $query->fetchAssoc()) {
      $account = user_load($uid['uid']);
      pum_notifications_update_next_notification($account);
      $account = null;
    }
  }

  // Find all users who should receive an email.
  $now = new DateTime();
  $now->setTime(23,59,59);

  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'user');
  $query->fieldCondition('field_next_notification', 'value', $now->format('Y-m-d H:i:s'), '<=');
  $query->propertyCondition('status', '1');
  $users = $query->execute();
  $count = 0;
  if (isset($users['user'])) {
    foreach($users['user'] as $uid => $u) {
      $user = clone $original_user;
      $account = user_load($uid, true);
      _pum_notifications_check_account_fields($account);
      $user = $account;
      $items = pum_notifications_get_items_for_user($account);
      if ($items) {
        $orderedItems = pum_notifications_order_items($items, $account);
        $mailMessage = pum_notifications_format_mail($orderedItems, $account);
        drupal_mail('pum_notifications', 'notification', $account->mail, user_preferred_language($account), array('message' => $mailMessage));
        $count++;
      }

      // Update the account to store the last and next notification day.
      $last_notification = new DateTime();
      $account->field_last_notification['und'][0]['value'] = $last_notification->format('Y-m-d H:i:s');
      pum_notifications_update_next_notification($account);
      $account = null;
    }
  }
  drush_print('Send '.$count.' e-mails');
}

function pum_notifications_format_mail($orderedItems, $account) {
  $countries = _pum_notification_get_term_options('countries');
  $sectors = _pum_notification_get_term_options('sector');
  $generalCommunities = _pum_notification_get_term_options('pum_generic');
  $privateCommunities = _pum_notification_get_term_options('private_group');
  $message = "";
  if (isset($orderedItems['countries']) && is_array($orderedItems['countries'])) {
    foreach ($orderedItems['countries'] as $country_id => $items) {
      $message .= "<h2>" . $countries[$country_id] . "</h2>\r\n";
      $message .= pum_notifications_format_items($items);
    }
  }
  if (isset($orderedItems['sectors']) && is_array($orderedItems['sectors'])) {
    foreach ($orderedItems['sectors'] as $sector_id => $items) {
      $message .= "<h2>" . $sectors[$sector_id] . "</h2>\r\n";
      $message .= pum_notifications_format_items($items);
    }
  }
  if (isset($orderedItems['generic']) && is_array($orderedItems['generic'])) {
    foreach ($orderedItems['generic'] as $generic_id => $items) {
      $message .= "<h2>" . $generalCommunities[$generic_id] . "</h2>\r\n";
      $message .= pum_notifications_format_items($items);
    }
  }
  if (isset($orderedItems['private']) && is_array($orderedItems['private'])) {
    foreach ($orderedItems['private'] as $private_id => $items) {
      $message .= "<h2>" . $privateCommunities[$private_id] . "</h2>\r\n";
      $message .= pum_notifications_format_items($items);
    }
  }
  if (isset($orderedItems['following']) && is_array($orderedItems['following']) && count($orderedItems['following'])) {
    $message .= "<h2>" . t('Following posts') . "</h2>\r\n";
    $message .= pum_notifications_format_items($orderedItems['following']);
  }
  return $message;
}

function pum_notifications_format_items($items) {
  $string = '';
  foreach($items as $item) {
    $updatedLabel = "";
    if ($item['type'] == 'comment') {
      $updatedLabel = ' '.t('New comment');
    } else if ($item['date_property'] == 'changed') {
      $updatedLabel = ' '. t('Updated');
    }
    $bundle = '';
    switch($item['bundle']) {
      case 'pum_forum_item':
        $iconUrl = url('/sites/all/themes/pum/images/icon-portal-forum-30x30.png', array('absolute' => TRUE));
        $bundle = '<img src="'.$iconUrl.'" title="'.t('Forum').'">';
        break;
      case 'pum_portal_blog':
        $iconUrl = url('/sites/all/themes/pum/images/icon-portal-blog-30x30.png', array('absolute' => TRUE));
        $bundle = '<img src="'.$iconUrl.'" title="'.t('Blog').'">';
        break;
      case 'pum_portal_document':
        $iconUrl = url('/sites/all/themes/pum/images/icon-portal-library-30x30.png', array('absolute' => TRUE));
        $bundle = '<img src="'.$iconUrl.'" title="'.t('Document').'">';
        break;
      case 'pum_portal_news':
        $iconUrl = url('/sites/all/themes/pum/images/icon-portal-news-30x30.png', array('absolute' => TRUE));
        $bundle = '<img src="'.$iconUrl.'" title="'.t('News').'">';
        break;
    }
    $string .= '<p>'.$bundle.'&nbsp;<a href="'.$item['link'].'">'.$item['title'].'</a><br /> <em>'.$item['author'].'</em> ('.$item['date'].$updatedLabel.')</p>';
    $string .= "\r\n";
  }
  return $string;
}

function pum_notifications_order_items($items, $account) {
  $orderedItems = array();
  $userCoutries = _pum_notifications_flatten_taxonomy_array($account->field_pum_country['und']);
  $userSectors = _pum_notifications_flatten_taxonomy_array($account->field_pum_sector['und']);
  foreach(_pum_notifications_flatten_taxonomy_array($account->field_pum_user_sector['und']) as $userSector) {
    if (!in_array($userSector, $userSectors)) {
      $userSectors[] = $userSector;
    }
  }
  $userGeneralCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_generic['und']);
  $userPrivateCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_private['und']);
  $followingPosts = _pum_notifications_flatten_entityreference_array($account->field_follow_portal_posts['und']);
  foreach($userCoutries as $country) {
    $countryItems = array();
    foreach($items as $item) {
      if (in_array($country, $item['country'])) {
        $countryItems[] = $item;
      }
    }
    if (count($countryItems)) {
      $orderedItems['countries'][$country] = $countryItems;
    }
  }
  foreach($userSectors as $sector) {
    $sectorItems = array();
    foreach($items as $item) {
      if (in_array($sector, $item['sector'])) {
        $sectorItems[] = $item;
      }
    }
    if (count($sectorItems)) {
      $orderedItems['sectors'][$sector] = $sectorItems;
    }
  }

  foreach($userGeneralCommunities as $generalCommunity) {
    $genericItems = array();
    foreach($items as $item) {
      if (in_array($generalCommunity, $item['generic'])) {
        $genericItems[] = $item;
      }
    }
    if (count($genericItems)) {
      $orderedItems['generic'][$generalCommunity] = $genericItems;
    }
  }
  foreach($userPrivateCommunities as $private) {
    $privateItems = array();
    foreach($items as $item) {
      if (in_array($private, $item['private'])) {
        $privateItems[] = $item;
      }
    }
    if (count($privateItems)) {
      $orderedItems['private'][$private] = $privateItems;
    }
  }

  foreach($followingPosts as $nid) {
    foreach($items as $item) {
      if ($item['nid'] == $nid) {
        $orderedItems['following'][] = $item;
      }
    }
  }
  return $orderedItems;
}

function pum_notifications_get_items_for_user($account) {
  $since = new DateTime($account->field_last_notification['und'][0]['value']);

  $coutryCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_country['und']);
  $sectorCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_sector['und']);
  $userSectorCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_user_sector['und']);
  $generalCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_generic['und']);
  $userPrivateCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_private['und']);

  $items = array();

  // Get new forum items
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_forum_item', $since, 'field_pum_country', $coutryCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_forum_item', $since, 'field_pum_sector', $sectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_forum_item', $since, 'field_pum_sector', $userSectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_forum_item', $since, 'field_pum_generic', $generalCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_forum_item', $since, 'field_pum_private', $userPrivateCommunities, array_keys($items));

  // Get new blog items
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_blog', $since, 'field_pum_country', $coutryCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_blog', $since, 'field_pum_sector', $sectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_blog', $since, 'field_pum_sector', $userSectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_blog', $since, 'field_pum_generic', $generalCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_blog', $since, 'field_pum_private', $userPrivateCommunities, array_keys($items));

  // Get new documents
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_country', $coutryCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_sector', $sectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_sector', $userSectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_generic', $generalCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_private', $userPrivateCommunities, array_keys($items));

  // Get changed documents
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_country', $coutryCommunities, array_keys($items), 'changed');
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_sector', $sectorCommunities, array_keys($items), 'changed');
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_sector', $userSectorCommunities, array_keys($items), 'changed');
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_generic', $generalCommunities, array_keys($items), 'changed');
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_document', $since, 'field_pum_private', $userPrivateCommunities, array_keys($items), 'changed');

  // Get new news items
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_news', $since, 'field_pum_country', $coutryCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_news', $since, 'field_pum_sector', $sectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_news', $since, 'field_pum_sector', $userSectorCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_news', $since, 'field_pum_generic', $generalCommunities, array_keys($items));
  $items = $items +pum_notifications_get_items_per_bundle_per_term($account,'pum_portal_news', $since, 'field_pum_private', $userPrivateCommunities, array_keys($items));

  // Get new comments on blogs I have written
  $myBlogs = pum_notifications_get_nodes_posted_by_user($account->uid, 'pum_portal_blog');
  $items = $items + pum_notifications_get_new_comments($account, $myBlogs, $since, $items);

  // Get new comments on forum topics I started
  $myForumPosts = pum_notifications_get_nodes_posted_by_user($account->uid, 'pum_forum_item');
  $myForumPosts = $myForumPosts + pum_notifications_get_nodes_with_a_comment_per_user($account->uid, 'pum_forum_item');
  $items = $items + pum_notifications_get_new_comments($account, $myForumPosts, $since, $items);

  // Get new comments on items I follow
  $followingPosts = _pum_notifications_flatten_entityreference_array($account->field_follow_portal_posts['und']);
  $items = $items + pum_notifications_get_new_comments($account, $followingPosts, $since, $items);
  return $items;
}

/**
 * Returns all nodes which are new or changed and which belong to a certain sector, country, generic community or private community
 *
 * @param $account,
 *   The user account
 * @param $bundle
 *   The name of the bundle, pum_portal_news, pum_portal_document, pum_portal_blog or pum_forum_item
 * @param $since
 *   Date
 * @param $taxonomy_field_name
 *   The taxonomy field name of the bundle
 * @param $terms
 *   The terms to check
 * @param $skipNids
 *   array, do not includes those nids
 * @param string $dateProperty
 *   created, changed which date field to use for checking the since date. Default to created
 *
 * @return array
 */
function pum_notifications_get_items_per_bundle_per_term($account, $bundle, $since, $taxonomy_field_name, $terms, $skipNids, $dateProperty='created') {
  $items = array();
  if (!is_array($terms)) {
    return $items;
  }
  if (!count($terms)) {
    return $items;
  }

  // Get all new nodes since the last time
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'node');
  $query->entityCondition('bundle', $bundle);
  $query->propertyCondition('status', '1');
  $query->propertyCondition($dateProperty, $since->getTimestamp(), '>');
  $query->propertyCondition('uid', $account->uid, '!=');
  $query->fieldCondition($taxonomy_field_name, 'tid', $terms, 'IN');
  if (is_array($skipNids) && count($skipNids)) {
    $query->propertyCondition('nid', $skipNids, 'NOT IN');
  }
  $results = $query->execute();
  if (isset($results['node'])) {
    $nodes = node_load_multiple(array_keys($results['node']));
    foreach($nodes as $node) {
      $changed = new DateTime();
      $changed->setTimestamp($node->$dateProperty);
      $author = user_load($node->uid);
      if (!isset($node->field_pum_sector['und']) || !is_array($node->field_pum_sector['und'])) {
        $node->field_pum_sector['und'] = array();
      }
      if (!isset($node->field_pum_country['und']) || !is_array($node->field_pum_country['und'])) {
        $node->field_pum_country['und'] = array();
      }
      if (!isset($node->field_pum_generic['und']) || !is_array($node->field_pum_generic['und'])) {
        $node->field_pum_generic['und'] = array();
      }
      if (!isset($node->field_pum_private['und']) || !is_array($node->field_pum_private['und'])) {
        $node->field_pum_private['und'] = array();
      }
      $item = array(
        'nid' => $node->nid,
        'bundle' => $node->type,
        'title' => $node->title,
        'date' => $changed->format('Y-m-d'),
        'link' => url('user/login', array('absolute' => true, 'query' => array('destination' => drupal_get_path_alias('/node/' . $node->nid, $node->language)))),
        'author' => $author->name,
        'sector' => _pum_notifications_flatten_taxonomy_array($node->field_pum_sector['und']),
        'country' => _pum_notifications_flatten_taxonomy_array($node->field_pum_country['und']),
        'generic' => _pum_notifications_flatten_taxonomy_array($node->field_pum_generic['und']),
        'private' => _pum_notifications_flatten_taxonomy_array($node->field_pum_private['und']),
        'date_property' => $dateProperty,
        'type' => 'node',
      );
      $items[$node->nid] = $item;
    }
  }
  return $items;
}

/**
 * Returns all nodes which have new comments
 *
 * @param $account
 *   The user account
 * @param $nidsToCheck
 *   array,
 * @param $since
 *   Date
 * @param $skipNids
 *   array, do not includes those nids
 * @return array
 */
function pum_notifications_get_new_comments($account, $nidsToCheck, $since, $skipNids) {
  $items = array();
  if (!is_array($nidsToCheck)) {
    return $items;
  }
  if (!count($nidsToCheck)) {
    return $items;
  }

  // Get all new nodes since the last time
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'comment');
  $query->propertyCondition('status', '1');
  $query->propertyCondition('created', $since->getTimestamp(), '>');
  $query->propertyCondition('nid', $nidsToCheck, 'IN');
  $query->propertyCondition('uid', $account->uid, '!=');
  $results = $query->execute();
  if (isset($results['comment'])) {
    $comments = comment_load_multiple(array_keys($results['comment']));
    foreach($comments as $comment) {
      if (!in_array($comment->nid, $skipNids)) {
        $node = node_load($comment->nid);
        $changed = new DateTime();
        $changed->setTimestamp($node->created);
        $author = user_load($node->uid);
        if (!isset($node->field_pum_sector['und']) || !is_array($node->field_pum_sector['und'])) {
          $node->field_pum_sector['und'] = [];
        }
        if (!isset($node->field_pum_country['und']) || !is_array($node->field_pum_country['und'])) {
          $node->field_pum_country['und'] = [];
        }
        if (!isset($node->field_pum_generic['und']) || !is_array($node->field_pum_generic['und'])) {
          $node->field_pum_generic['und'] = [];
        }
        if (!isset($node->field_pum_private['und']) || !is_array($node->field_pum_private['und'])) {
          $node->field_pum_private['und'] = [];
        }
        $item = [
          'nid' => $node->nid,
          'bundle' => $node->type,
          'title' => $node->title,
          'date' => $changed->format('Y-m-d'),
          'link' => url('user/login', array('absolute' => true, 'query' => array('destination' => drupal_get_path_alias('/node/' . $node->nid, $node->language)))),
          'author' => $author->name,
          'sector' => _pum_notifications_flatten_taxonomy_array($node->field_pum_sector['und']),
          'country' => _pum_notifications_flatten_taxonomy_array($node->field_pum_country['und']),
          'generic' => _pum_notifications_flatten_taxonomy_array($node->field_pum_generic['und']),
          'private' => _pum_notifications_flatten_taxonomy_array($node->field_pum_private['und']),
          'date_property' => 'created',
          'type' => 'comment',
        ];
        $items[$node->nid] = $item;

        $skipNids[] = $node->nid;
      }
    }
  }
  return $items;
}

/**
 * Get all node ids from a certain bundle which are posted by $uid.
 * @param $uid
 * @param $bundle
 *
 * @return array
 */
function pum_notifications_get_nodes_posted_by_user($uid, $bundle) {
  // Get all new nodes since the last time
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'node');
  $query->entityCondition('bundle', $bundle);
  $query->propertyCondition('status', '1');
  $query->propertyCondition('uid', $uid);
  $results = $query->execute();
  if (isset($results['node'])) {
    return array_keys($results['node']);
  }
  return array();
}

/**
 * Returns all node ids from a certain bundle where the $uid has posted a comment to.
 *
 * @param $uid
 * @param $bundle
 *
 * @return array
 */
function pum_notifications_get_nodes_with_a_comment_per_user($uid, $bundle) {
  $query = new EntityFieldQuery();
  $query->entityCondition('entity_type', 'comment');
  $query->propertyCondition('status', '1');
  $query->propertyCondition('uid', $uid);
  $results = $query->execute();
  if (isset($results['comment'])) {
    $comments = comment_load_multiple(array_keys($results['comment']));
    $nids = array();
    foreach($comments as $comment) {
      $nids[] = $comment->nid;
    }

    // Now check whether the nodes are of the correct bundle
    $query = new EntityFieldQuery();
    $query->entityCondition('entity_type', 'node');
    $query->entityCondition('bundle', $bundle);
    $query->propertyCondition('status', '1');
    $query->propertyCondition('nid', $nids, 'IN');
    $results = $query->execute();
    if (isset($results['node'])) {
      return array_keys($results['node']);
    }
  }
  return array();
}