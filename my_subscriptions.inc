<?php

function pum_notifications_my_subscriptions($form, &$form_state) {
  global $user;

  // Check whether the unfollow button is pressed, if so un follow the topic.
  if (isset($form_state['input']) && isset($form_state['input']['_triggering_element_name']) && stripos($form_state['input']['_triggering_element_name'], 'unfollow_')===0) {
    $unfollow_nid = str_replace('unfollow_', '', $form_state['input']['_triggering_element_name']);
    pum_notifications_unfollow_post($user->uid, $unfollow_nid);
  }

  $account = user_load($user->uid, true);
  $form_state['account'] = $account;

  _pum_notifications_check_account_fields($account);

  $countries = _pum_notification_get_term_options('countries');
  $selectedCoutries = _pum_notifications_flatten_taxonomy_array($account->field_pum_country['und']);
  $sectors = _pum_notification_get_term_options('sector');
  $selectedSectors = _pum_notifications_flatten_taxonomy_array($account->field_pum_sector['und']);
  $generalCommunities = _pum_notification_get_term_options('pum_generic');
  $selectedGeneralCommunities = _pum_notifications_flatten_taxonomy_array($account->field_pum_generic['und']);
  $privateCommunities = _pum_notification_get_term_options('private_group');
  $followingPosts = _pum_notifications_flatten_entityreference_array($account->field_follow_portal_posts['und']);
  $selectedPrivateCommunities = array();
  foreach($account->field_pum_private['und'] as $selectedTerm) {
    $selectedPrivateCommunities[] = $privateCommunities[$selectedTerm['tid']];
  }
  $selectedUserSectors = array();
  foreach($account->field_pum_user_sector['und'] as $selectedTerm) {
    $selectedUserSectors[] = $sectors[$selectedTerm['tid']];
  }

  $frequencyField = field_info_field('field_email_frequency');

  $form['communities']['#type'] = 'fieldset';
  $form['communities']['#title'] = t('Following communities');
  $form['communities']['country']['#title'] = t('Country communities');
  $form['communities']['country']['#type'] = 'select';
  $form['communities']['country']['#options'] = $countries;
  $form['communities']['country']['#default_value'] = $selectedCoutries;
  $form['communities']['country']['#multiple'] = TRUE;

  $form['communities']['sector']['#title'] = t('Sector communities');
  $form['communities']['sector']['#type'] = 'select';
  $form['communities']['sector']['#options'] = $sectors;
  $form['communities']['sector']['#default_value'] = $selectedSectors;
  $form['communities']['sector']['#multiple'] = TRUE;

  $form['communities']['user_sector']['#type'] = 'markup';
  $form['communities']['user_sector']['#markup'] = t('Your sector(s)').': '. implode(', ', $selectedUserSectors);

  $form['communities']['general_communities']['#title'] = t('General communities');
  $form['communities']['general_communities']['#type'] = 'select';
  $form['communities']['general_communities']['#options'] = $generalCommunities;
  $form['communities']['general_communities']['#default_value'] = $selectedGeneralCommunities;
  $form['communities']['general_communities']['#multiple'] = TRUE;

  $form['communities']['private_communities']['#type'] = 'markup';
  $form['communities']['private_communities']['#markup'] = t('Private communities').': '. implode(', ', $selectedPrivateCommunities);

  $form['following_posts']['#type'] = 'fieldset';
  $form['following_posts']['#title'] = t('Following posts');
  if (count($followingPosts)) {
    $followingPostNodes = node_load_multiple($followingPosts);
    foreach ($followingPostNodes as $node) {
      $link = '';
      if ($node->language != 'en') {
        $link = $node->language.'/';
      }
      $link .= drupal_get_path_alias('node/' . $node->nid, $node->language);
      $nodeUrl = url($link, array('absolute' => true));
      $form['following_posts']['remove_'.$node->nid]['#type'] = 'button';
      $form['following_posts']['remove_'.$node->nid]['#name'] = 'unfollow_'.$node->nid;
      $form['following_posts']['remove_'.$node->nid]['#value'] = t('Unfollow');
      $form['following_posts']['remove_'.$node->nid]['#description'] = $node->title;
      $form['following_posts']['remove_'.$node->nid]['#prefix'] = "<p><a href=\"{$nodeUrl}\">{$node->title}</a>&nbsp;";
      $form['following_posts']['remove_'.$node->nid]['#suffix'] = '</p>';
      $form['following_posts']['remove_'.$node->nid]['#ajax'] = array(
        'callback' => 'pum_notifications_following_posts_callback',
        'wrapper' => 'edit-following-posts',
        'method' => 'replace'
      );
    }
  } else {
    $form['following_posts']['no_follow']['#markup'] = '<p>'.t('You don\'t follow individual posts').'</p>';
  }

  $form['frequency_settings']['#type'] = 'fieldset';
  $form['frequency_settings']['#title'] = t('Frequency of e-mail updates');
  $form['frequency_settings']['frequency']['#type'] = 'select';
  $form['frequency_settings']['frequency']['#options'] = $frequencyField['settings']['allowed_values'];
  $form['frequency_settings']['frequency']['#default_value'] = $account->field_email_frequency['und'][0]['value'];

  $form['submit']['#type'] = 'submit';
  $form['submit']['#value'] = t('Update your subscription settings');
  $form['submit']['#submit'] = array('pum_notifications_my_subscriptions_submit');

  return $form;
}

function pum_notifications_my_subscriptions_submit($form, &$form_state) {
  $account = $form_state['account'];
  $account->field_pum_country['und'] = array();
  foreach($form_state['values']['country'] as $country_tid) {
    $account->field_pum_country['und'][] = array('tid' => $country_tid);
  }

  $account->field_pum_sector['und'] = array();
  foreach($form_state['values']['sector'] as $sector_tid) {
    $account->field_pum_sector['und'][] = array('tid' => $sector_tid);
  }

  $account->field_pum_generic['und'] = array();
  foreach($form_state['values']['general_communities'] as $general_tid) {
    $account->field_pum_generic['und'][] = array('tid' => $general_tid);
  }

  $account->field_email_frequency['und'][0]['value'] = $form_state['values']['frequency'];

  user_save($account);
  pum_notifications_update_next_notification($account);
  drupal_set_message(t('Successfully updated your subscription preferences'));
  $form_state['redirect'] = 'portal';
}

function pum_notifications_following_posts_callback($form, &$form_state) {
  return $form['following_posts'];
}